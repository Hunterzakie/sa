/*****************************************************************************
 * Copyright (C) 2020 VLC authors and VideoLAN
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
import QtQuick.Controls
import QtQuick.Templates as T


import VLC.MainInterface
import VLC.Style
import VLC.Widgets as Widgets

T.Button {
    id: control

    property color color: theme.fg.primary
    property color backgroundColor: theme.bg.primary
    property string description

    property int textStyle: Text.Normal
    property color textStyleColor: theme.bg.secondary

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitContentHeight + topPadding + bottomPadding)

    font.family: VLCIcons.fontFamily

    Keys.priority: Keys.AfterItem
    Keys.onPressed: (event) => Navigation.defaultKeyAction(event)

    //Accessible
    Accessible.onPressAction: control.clicked()
    Accessible.name: description

    // Tooltip
    T.ToolTip.visible: (hovered || visualFocus)
    T.ToolTip.delay: VLCStyle.delayToolTipAppear
    T.ToolTip.text: description

    readonly property ColorContext colorContext : ColorContext {
        id: theme
        colorSet: ColorContext.ButtonStandard

        enabled: control.enabled
        focused: control.visualFocus
        hovered: control.hovered
        pressed: control.down
    }

    contentItem: Widgets.IconLabel {
        font: control.font
        color: control.checked ? theme.accent : control.color
        text: control.text
        style: control.textStyle
        styleColor: control.textStyleColor
    }

    background: Widgets.AnimatedBackground {
        enabled: theme.initialized

        color: control.backgroundColor

        border.color: control.visualFocus ? theme.visualFocus : theme.border
    }
}
