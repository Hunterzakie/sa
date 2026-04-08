/*****************************************************************************
 * Copyright (C) 2021 VLC authors and VideoLAN
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
import QtQuick


import VLC.Widgets as Widgets
import VLC.Player
import VLC.Style
import VLC.Util

Widgets.ImageToolButton {
    id: control

    text: qsTr("A to B Loop")

    sourceSize: Qt.size(VLCStyle.icon_toolbar, VLCStyle.icon_toolbar)

    checked: MainPlayerController.ABloopState !== MainPlayerController.ABLOOP_STATE_NONE
    onClicked: MainPlayerController.toggleABloopState()

    description: text

    //imageSource: "qrc:///icons/atob.svg"
    imageSource: {
        switch(MainPlayerController.ABloopState) {
        case MainPlayerController.ABLOOP_STATE_A:
            return control._colorize(
                control.colorContext.accent,
                control.colorContext.fg.primary
            )
        case MainPlayerController.ABLOOP_STATE_B:
            return control._colorize(
                control.colorContext.accent,
                control.colorContext.accent
            )
        case MainPlayerController.ABLOOP_STATE_NONE:
        default:
            return control._colorize(
                control.colorContext.fg.primary,
                control.colorContext.fg.primary
            )
        }
    }

    function _colorize(a, b) {
        return SVGColorImage.colorize("qrc:///icons/atob.svg")
            .color1(control.colorContext.fg.primary)
            .any({
                "#AAAAAA": a,
                "#BBBBBB": b,
            }).uri()
    }
}
