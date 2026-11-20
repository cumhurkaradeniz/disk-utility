// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: BSD-3-Clause

#include "dialogs/identity_guard.h"

#include "views/context.h"

#include "udisks/model.h"

#include "base/i18n.h"
#include "ui/dialog_window.h"

using cutefish::i18n::format;
using cutefish::i18n::tr;

namespace dialogs {

void showGone(views::Context& context, const std::string& name)
{
    if (context.showError)
        context.showError(format(tr("“{name}” is no longer there."), {{"name", name}}),
                          tr("It was removed or replaced by another disk, so nothing more is done to it."));
}

IdentityGuard::IdentityGuard(views::Context& context, std::vector<udisks::Identity> identities, std::string name,
                             std::function<void()> close)
    : m_context(context)
    , m_name(std::move(name))
    , m_close(std::move(close))
    , m_identities(identities)
    , m_watch(std::make_unique<udisks::IdentityWatch>(context.model, std::move(identities), [this] { lost(); }))
{
}

IdentityGuard::IdentityGuard(views::Context& context, std::vector<udisks::Identity> identities, std::string name,
                             cutefish::ui::DialogWindow& dialog, std::function<bool()> busy)
    : m_context(context)
    , m_name(std::move(name))
    , m_dialog(&dialog)
    , m_busy(std::move(busy))
    , m_identities(identities)
    , m_watch(std::make_unique<udisks::IdentityWatch>(context.model, std::move(identities), [this] { lost(); }))
{
}

bool IdentityGuard::holds() const
{
    for (const udisks::Identity& identity : m_identities) {
        if (!m_context.model.matches(identity))
            return false;
    }
    return true;
}

void IdentityGuard::disarm()
{
    m_watch.reset();
}

void IdentityGuard::lost()
{
    if (m_dialog) {
        // A dialog closed by its own Done or Escape has nothing left to guard.
        if (!m_dialog->window()) {
            disarm();
            return;
        }
        if (m_busy && m_busy())
            return;
        disarm();
        m_dialog->close();
        showGone(m_context, m_name);
        return;
    }
    disarm();
    // The callback may destroy the guard.
    const std::function<void()> close = m_close;
    const std::string name = m_name;
    views::Context& context = m_context;
    if (close)
        close();
    showGone(context, name);
}

} // namespace dialogs
