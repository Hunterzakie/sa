/*****************************************************************************
 * Copyright (C) 2024 VLC authors and VideoLAN
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * ( at your option ) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
 *****************************************************************************/
#ifndef WINDOWEFFECTSMODULE_HPP
#define WINDOWEFFECTSMODULE_HPP

#include <vlc_common.h>

#include <QRegion>

class QWindow;

struct WindowEffectsModule
{
    enum Effect {
        BlurBehind
    };

    vlc_object_t obj;

    module_t *p_module = nullptr;
    void *p_sys = nullptr;

    // NOTE: For optimization purposes, if the module does not
    //       support applying effects in a partial area, such
    //       as with the `Win32WindowEffects` module, you may
    //       choose to not spend time on calculating the region:
    bool (*supportsPartialArea)(WindowEffectsModule& obj, Effect effect);

    bool (*isEffectAvailable)(WindowEffectsModule& obj, const QWindow* window, Effect effect);

    // NOTE: Region is not guaranteed to be respected, see `supportsPartialArea()`.
    // WARNING: Care must be taken for Qt library ABI compatibility.
    //          For example, KWindowSystem may be built with Qt that are not
    //          binary compatible with the version the application uses.
    // WARNING: It is not permitted to call `setBlurBehind()` if `isEffectAvailable()` returned
    //          false for `Effect::BlurBehind` effect.
    void (*setBlurBehind)(WindowEffectsModule& obj, QWindow* window, bool enable, const QRegion& region);

    // NOTE: Returns true if you should not place controls directly on top of the effect:
    bool (*blurBehindRequiresOpacityLayer)(WindowEffectsModule& obj);
};

#endif // WINDOWEFFECTSMODULE_HPP
