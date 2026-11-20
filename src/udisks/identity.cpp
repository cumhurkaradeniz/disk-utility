// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: BSD-3-Clause

#include "udisks/identity.h"
#include "udisks/model.h"

namespace udisks {

static void fillDisk(const Model& model, const Object& block, Identity& identity)
{
    identity.drive = block.block->drive;
    if (const Object* drive = model.object(identity.drive); drive && drive->drive) {
        identity.driveId = drive->drive->id;
        identity.serial = drive->drive->serial;
        identity.wwn = drive->drive->wwn;
    }
    if (block.loop)
        identity.backingFile = block.loop->backingFile;
}

Identity identityOf(const Model& model, const std::string& path)
{
    Identity identity;
    const Object* block = model.object(path);
    if (!block || !block->block)
        return identity;
    identity.block = path;
    identity.size = block->block->size;
    fillDisk(model, *block, identity);
    if (block->partition) {
        identity.partition = true;
        identity.offset = block->partition->offset;
        identity.partitionUuid = block->partition->uuid;
        if (const Object* table = model.object(block->partition->table); table && table->loop)
            identity.backingFile = table->loop->backingFile;
    } else if (!block->block->cryptoBackingDevice.empty()) {
        // A cleartext block has no drive of its own: its encrypted one's.
        const Identity backing = identityOf(model, block->block->cryptoBackingDevice);
        identity.drive = backing.drive;
        identity.driveId = backing.driveId;
        identity.serial = backing.serial;
        identity.wwn = backing.wwn;
        identity.backingFile = backing.backingFile;
    }
    return identity;
}

Identity diskIdentityOf(const Model& model, const std::string& path)
{
    const Object* block = model.object(path);
    if (block && block->block && !block->block->cryptoBackingDevice.empty())
        return diskIdentityOf(model, block->block->cryptoBackingDevice);
    if (block && block->partition)
        return identityOf(model, block->partition->table);
    return identityOf(model, path);
}

bool Model::matches(const Identity& identity) const
{
    return identity.isValid() && identityOf(*this, identity.block) == identity;
}

IdentityWatch::IdentityWatch(Model& model, std::vector<Identity> identities, std::function<void()> onLost)
    : m_model(model)
    , m_identities(std::move(identities))
    , m_onLost(std::move(onLost))
{
    m_subscription = m_model.changed.connect([this] { check(); });
}

bool IdentityWatch::holds() const
{
    for (const Identity& identity : m_identities) {
        if (!m_model.matches(identity))
            return false;
    }
    return true;
}

void IdentityWatch::check()
{
    if (m_lost || holds())
        return;
    m_lost = true;
    // The callback may destroy the watch.
    const std::function<void()> onLost = m_onLost;
    if (onLost)
        onLost();
}

} // namespace udisks
