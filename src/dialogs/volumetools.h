// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

#include <memory>

namespace views {
struct Context;
}

namespace dialogs {

class FirstAid;
class InfoWindow;
class Mounting;
class RenameDialog;

// The window's handlers for First Aid, Get Info, Rename, Mount, Unmount,
// Lock and Eject, and the dialogs they open.
class VolumeTools
{
public:
    explicit VolumeTools(views::Context& context);
    ~VolumeTools();

    VolumeTools(const VolumeTools&) = delete;
    VolumeTools& operator=(const VolumeTools&) = delete;

    InfoWindow* info() const { return m_info.get(); }

private:
    views::Context& m_context;
    std::unique_ptr<FirstAid> m_firstAid;
    std::unique_ptr<Mounting> m_mounting;
    std::unique_ptr<InfoWindow> m_info;
    std::unique_ptr<RenameDialog> m_rename;
};

} // namespace dialogs
