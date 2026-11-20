// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

#include "udisks/identity.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace cutefish::ui {
class DialogWindow;
}

namespace views {
struct Context;
}

namespace dialogs {

// A dialog's hold on the blocks it was opened on: once one of them is gone,
// or another disk or image took its path, the dialog is closed at once and
// the window says why. Checked on every change of the model, until the
// guard is disarmed.
class IdentityGuard
{
public:
    // Calls close on the loss, whatever the dialog is doing.
    IdentityGuard(views::Context& context, std::vector<udisks::Identity> identities, std::string name,
                  std::function<void()> close);
    // Closes the dialog on the loss, except while busy() says a run is under
    // way: the run stops before its next step and reports the loss itself,
    // and the dialog stays to show it. Disarms itself once the dialog has
    // closed, so a later loss says nothing.
    IdentityGuard(views::Context& context, std::vector<udisks::Identity> identities, std::string name,
                  cutefish::ui::DialogWindow& dialog, std::function<bool()> busy = {});

    IdentityGuard(const IdentityGuard&) = delete;
    IdentityGuard& operator=(const IdentityGuard&) = delete;

    // Every block is still the one it was.
    bool holds() const;
    // Watches no more: the confirmation it guarded was answered, or its
    // dialog closed. Safe from the guard's own callback.
    void disarm();
    bool isArmed() const { return m_watch != nullptr; }

private:
    void lost();

    views::Context& m_context;
    std::string m_name;
    std::function<void()> m_close;
    cutefish::ui::DialogWindow* m_dialog = nullptr;
    std::function<bool()> m_busy;
    std::vector<udisks::Identity> m_identities;
    std::unique_ptr<udisks::IdentityWatch> m_watch;
};

// The window's alert for a block that went while a dialog was open on it.
void showGone(views::Context& context, const std::string& name);

} // namespace dialogs
