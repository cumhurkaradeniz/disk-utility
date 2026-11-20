// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

#include "udisks/identity.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace views {
struct Context;
}

namespace dialogs {

// A dialog's hold on the blocks it was opened on: once one of them is gone,
// or another disk or image took its path, the dialog is closed at once and
// the window says why. Checked on every change of the model.
class IdentityGuard
{
public:
    IdentityGuard(views::Context& context, std::vector<udisks::Identity> identities, std::string name,
                  std::function<void()> close);

    IdentityGuard(const IdentityGuard&) = delete;
    IdentityGuard& operator=(const IdentityGuard&) = delete;

    // Every block is still the one it was.
    bool holds() const { return m_watch->holds(); }

private:
    std::unique_ptr<udisks::IdentityWatch> m_watch;
};

// The window's alert for a block that went while a dialog was open on it.
void showGone(views::Context& context, const std::string& name);

} // namespace dialogs
