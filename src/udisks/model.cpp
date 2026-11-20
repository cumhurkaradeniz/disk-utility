// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: BSD-3-Clause

#include "udisks/model.h"

#include <algorithm>

namespace udisks {

void Model::reparse(Object& object, const std::string& interface)
{
    const auto found = object.properties.find(interface);
    parseInterface(object, interface, found == object.properties.end() ? nullptr : &found->second);
}

void Model::reset(const Objects& objects)
{
    std::map<std::string, Object> next;
    for (const auto& [path, interfaces] : objects) {
        Object& object = next[path];
        object.path = path;
        object.properties = interfaces;
        for (const auto& [interface, properties] : interfaces)
            reparse(object, interface);
    }

    const std::map<std::string, Object> previous = std::exchange(m_objects, std::move(next));
    m_ready = true;
    for (const auto& [path, object] : previous) {
        if (!m_objects.contains(path))
            objectRemoved.emit(path);
    }
    for (const auto& [path, object] : m_objects) {
        const auto old = previous.find(path);
        if (old == previous.end())
            objectAdded.emit(path);
        else if (old->second.properties != object.properties)
            objectChanged.emit(path);
    }
    changed.emit();
}

void Model::addInterfaces(const std::string& path, const Interfaces& interfaces)
{
    const bool added = !m_objects.contains(path);
    Object& object = m_objects[path];
    object.path = path;
    for (const auto& [interface, properties] : interfaces) {
        object.properties[interface] = properties;
        reparse(object, interface);
    }
    if (added)
        objectAdded.emit(path);
    else
        objectChanged.emit(path);
    changed.emit();
}

void Model::removeInterfaces(const std::string& path, const std::vector<std::string>& interfaces)
{
    const auto found = m_objects.find(path);
    if (found == m_objects.end())
        return;
    Object& object = found->second;
    for (const std::string& interface : interfaces) {
        object.properties.erase(interface);
        reparse(object, interface);
    }
    if (object.properties.empty()) {
        m_objects.erase(found);
        objectRemoved.emit(path);
    } else {
        objectChanged.emit(path);
    }
    changed.emit();
}

void Model::changeProperties(const std::string& path, const std::string& interface, const VariantMap& changedValues,
                             const std::vector<std::string>& invalidated)
{
    const auto found = m_objects.find(path);
    if (found == m_objects.end())
        return;
    Object& object = found->second;
    VariantMap& properties = object.properties[interface];
    for (const auto& [name, value] : changedValues)
        properties[name] = value;
    for (const std::string& name : invalidated)
        properties.erase(name);
    reparse(object, interface);
    objectChanged.emit(path);
    changed.emit();
}

void Model::setManager(const VariantMap& properties)
{
    m_manager = parseManager(properties);
    changed.emit();
}

void Model::setSystemDevices(SystemDevices devices)
{
    if (devices == m_system)
        return;
    m_system = std::move(devices);
    changed.emit();
}

const Object* Model::object(const std::string& path) const
{
    const auto found = m_objects.find(path);
    return found == m_objects.end() ? nullptr : &found->second;
}

const Object* Model::wholeBlock(const std::string& drive) const
{
    if (drive.empty())
        return nullptr;
    const Object* best = nullptr;
    for (const auto& [path, object] : m_objects) {
        if (!object.block || object.block->drive != drive || object.partition
            || !object.block->cryptoBackingDevice.empty())
            continue;
        // Several paths to one disk (multipath): the one with a table, else
        // the first.
        if (!best || (object.partitionTable && !best->partitionTable))
            best = &object;
    }
    return best;
}

std::vector<const Object*> Model::blocksOfDrive(const std::string& drive) const
{
    std::vector<const Object*> blocks;
    if (drive.empty())
        return blocks;
    for (const auto& [path, object] : m_objects) {
        if (object.block && object.block->drive == drive)
            blocks.push_back(&object);
    }
    const size_t own = blocks.size();
    for (size_t i = 0; i < own; ++i) {
        const Object* clear = cleartextOf(blocks[i]->path);
        if (clear && std::find(blocks.begin(), blocks.end(), clear) == blocks.end())
            blocks.push_back(clear);
    }
    return blocks;
}

std::vector<const Object*> Model::partitionsOf(const std::string& table) const
{
    std::vector<const Object*> partitions;
    for (const auto& [path, object] : m_objects) {
        if (object.block && object.partition && object.partition->table == table)
            partitions.push_back(&object);
    }
    std::sort(partitions.begin(), partitions.end(), [](const Object* a, const Object* b) {
        if (a->partition->offset != b->partition->offset)
            return a->partition->offset < b->partition->offset;
        return a->partition->number < b->partition->number;
    });
    return partitions;
}

const Object* Model::cleartextOf(const std::string& encrypted) const
{
    const Object* backing = object(encrypted);
    if (!backing || !backing->encrypted)
        return nullptr;
    if (const Object* clear = object(backing->encrypted->cleartextDevice); clear && clear->block)
        return clear;
    // CleartextDevice can lag behind the cleartext block's own link.
    for (const auto& [path, object] : m_objects) {
        if (object.block && object.block->cryptoBackingDevice == encrypted)
            return &object;
    }
    return nullptr;
}

const Object* Model::contentOf(const std::string& block) const
{
    if (const Object* clear = cleartextOf(block))
        return clear;
    return object(block);
}

std::vector<const Object*> Model::jobsFor(const std::string& path) const
{
    std::vector<std::string> targets{path};
    if (const Object* subject = object(path); subject && subject->drive) {
        for (const Object* block : blocksOfDrive(path))
            targets.push_back(block->path);
    } else if (const Object* clear = cleartextOf(path)) {
        targets.push_back(clear->path);
    }
    std::vector<const Object*> jobs;
    for (const auto& [jobPath, object] : m_objects) {
        if (!object.job)
            continue;
        const bool matches = std::any_of(object.job->objects.begin(), object.job->objects.end(),
                                         [&](const std::string& target) {
                                             return std::find(targets.begin(), targets.end(), target)
                                                     != targets.end();
                                         });
        if (matches)
            jobs.push_back(&object);
    }
    return jobs;
}

std::vector<const Object*> Model::logicalPartitionsOf(const std::string& container) const
{
    const Object* subject = object(container);
    if (!subject || !subject->partition || !subject->partition->isContainer)
        return {};
    std::vector<const Object*> inside;
    const Partition& outer = *subject->partition;
    for (const Object* partition : partitionsOf(outer.table)) {
        const Partition& inner = *partition->partition;
        if (partition != subject && inner.isContained && inner.offset >= outer.offset
            && inner.offset < outer.offset + outer.size)
            inside.push_back(partition);
    }
    return inside;
}

bool Model::holdsSystemItself(const std::string& path) const
{
    const Object* subject = object(path);
    if (!subject || !subject->block)
        return false;
    if (m_system.contains(subject->block->deviceNumber))
        return true;
    for (const Object* part : {subject, contentOf(path)}) {
        if (!part || !part->block)
            continue;
        if (m_system.contains(part->block->deviceNumber))
            return true;
        if (part->filesystem
            && std::any_of(part->filesystem->mountPoints.begin(), part->filesystem->mountPoints.end(),
                           isSystemMountPoint))
            return true;
        if (part->swapspace && part->swapspace->active)
            return true;
    }
    if (subject->partitionTable) {
        for (const Object* partition : partitionsOf(path)) {
            if (holdsSystemItself(partition->path))
                return true;
        }
    }
    if (subject->partition && subject->partition->isContainer) {
        for (const Object* partition : logicalPartitionsOf(path)) {
            if (holdsSystemItself(partition->path))
                return true;
        }
    }
    return false;
}

bool Model::holdsSystem(const std::string& path) const
{
    const Object* subject = object(path);
    if (!subject || !subject->block)
        return false;
    if (!m_system.known)
        return true;
    if (holdsSystemItself(path))
        return true;
    // The firmware's and the boot loader's partitions of the system's disk,
    // which need not be mounted (an automount shows nothing until used).
    if (subject->partition && isBootPartitionType(subject->partition->type)) {
        for (const Object* partition : partitionsOf(subject->partition->table)) {
            if (partition != subject && holdsSystemItself(partition->path))
                return true;
        }
    }
    return false;
}



bool Model::driveHoldsSystem(const std::string& drive) const
{
    for (const Object* block : blocksOfDrive(drive)) {
        if (holdsSystem(block->path))
            return true;
    }
    return false;
}

bool Model::isInUse(const std::string& path) const
{
    const Object* subject = object(path);
    if (!subject || !subject->block)
        return false;
    for (const Object* part : {subject, contentOf(path)}) {
        if (!part)
            continue;
        if (part->filesystem && !part->filesystem->mountPoints.empty())
            return true;
        if (part->swapspace && part->swapspace->active)
            return true;
    }
    if (subject->partitionTable) {
        for (const Object* partition : partitionsOf(path)) {
            if (isInUse(partition->path))
                return true;
        }
    }
    if (subject->partition && subject->partition->isContainer) {
        for (const Object* partition : logicalPartitionsOf(path)) {
            if (isInUse(partition->path))
                return true;
        }
    }
    return false;
}

} // namespace udisks
