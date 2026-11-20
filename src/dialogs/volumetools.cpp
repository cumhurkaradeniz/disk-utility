// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: BSD-3-Clause

#include "dialogs/volumetools.h"

#include "dialogs/firstaid.h"
#include "dialogs/info.h"
#include "dialogs/mount.h"
#include "dialogs/rename.h"
#include "views/context.h"

namespace dialogs {

VolumeTools::VolumeTools(views::Context& context)
    : m_context(context)
    , m_firstAid(std::make_unique<FirstAid>(context))
    , m_mounting(std::make_unique<Mounting>(context))
{
    context.actions.setHandler(Action::Info, [this](const Selection& selection) {
        if (!selection)
            return;
        m_info = std::make_unique<InfoWindow>(m_context, *selection);
        m_info->show();
    });
    context.actions.setHandler(Action::Rename, [this](const Selection& selection) {
        if (!selection)
            return;
        m_rename = std::make_unique<RenameDialog>(m_context, *selection);
        m_rename->show();
    });
}

VolumeTools::~VolumeTools()
{
    m_context.actions.setHandler(Action::Info, {});
    m_context.actions.setHandler(Action::Rename, {});
}

} // namespace dialogs
