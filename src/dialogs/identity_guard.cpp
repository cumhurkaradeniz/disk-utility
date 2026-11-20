// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: BSD-3-Clause

#include "dialogs/identity_guard.h"

#include "views/context.h"

#include "base/i18n.h"

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
    : m_watch(std::make_unique<udisks::IdentityWatch>(
          context.model, std::move(identities), [&context, name = std::move(name), close = std::move(close)] {
              if (close)
                  close();
              showGone(context, name);
          }))
{
}

} // namespace dialogs
