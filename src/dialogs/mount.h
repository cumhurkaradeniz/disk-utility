// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

#include "actions.h"
#include "udisks/backend.h"

#include <memory>
#include <string>
#include <vector>

namespace views {
struct Context;
}

namespace dialogs {

class UnlockDialog;

// The name the sidebar shows for a volume's block, its cleartext block
// included; the label or the device when it is not in the sidebar.
std::string volumeName(const views::Context& context, const std::string& block);

// Mount, Unmount, Lock and Eject. A locked volume asks for its passphrase
// before it mounts; a disk's volumes go one after another. A volume still
// in use is named in an alert with the programs using it.
class Mounting
{
public:
    explicit Mounting(views::Context& context);
    ~Mounting();

    Mounting(const Mounting&) = delete;
    Mounting& operator=(const Mounting&) = delete;

    void mount(const Selection& selection);
    void unmount(const Selection& selection);
    void lock(const Selection& selection);
    void eject(const Selection& selection);

private:
    enum class Kind {
        Unlock,
        Mount,
        Unmount,
        Lock,
        // The block is the whole disk.
        Eject,
    };
    struct Step
    {
        Kind kind;
        std::string block;
        // The volume's name, for the alert.
        std::string name;
    };

    void unmountSteps(const udisks::Object* volume, std::vector<Step>& steps) const;
    void start(std::vector<Step> steps);
    void next();
    void failed(const Step& step, const udisks::Result& result, const std::vector<std::string>& mountPoints);
    std::vector<std::string> mountPointsOf(const std::string& block) const;

    views::Context& m_context;
    std::vector<Step> m_steps;
    size_t m_next = 0;
    std::unique_ptr<UnlockDialog> m_unlock;
    std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);
};

} // namespace dialogs
