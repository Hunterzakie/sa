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
import VLC.MainInterface
import VLC.Style
import VLC.Util

// targetWidth: concerned widget's current width
// HorizontalResizeHandle actually doesn't resizes target
// you have to assign target's width manually using requestedWidth property
// e.g target.width: Helpers.clamp(requestedWidth, minimumWidth, maximumWidth)
MouseArea {
    id: root

    required property int currentWidth
    property int requestedWidth: 0
    property int minimumWidth: 0
    property int maximumWidth: Number.MAX_SAFE_INTEGER

    property bool atRight: true

    property alias handleVisual: handleVisual
    property alias hoverLine: resizeHoverLine

    property bool highlight: root.containsMouse || root.pressed

    property int _previousX

    readonly property ColorContext colorContext: ColorContext {
        id: theme
    }

    cursorShape: enabled ? Qt.SplitHCursor : undefined
    width: VLCStyle.resizeHandleWidth
    acceptedButtons: Qt.LeftButton
    preventStealing: true

    onPressed: (mouse) => {
        MainCtx.setCursor(cursorShape)
        _previousX = mouseX
    }

    onReleased:(mouse) => {
        MainCtx.restoreCursor()
    }

    onCanceled: {
        MainCtx.restoreCursor()
    }

    onPositionChanged: {
        if (!pressed)
            return

        const f = atRight ? 1 : -1
        const delta = mouseX - _previousX
        root.requestedWidth = Helpers.clamp(
            root.currentWidth + (delta * f),
            minimumWidth,
            maximumWidth)
    }

    Rectangle {
        id: handleVisual

        anchors {
            top: parent.top
            bottom: parent.bottom
            left: root.atRight ? undefined : parent.left
            right: root.atRight ? parent.right: undefined
        }

        width: VLCStyle.border
        color: theme.separator
    }

    Rectangle {
        id: resizeHoverLine

        readonly property bool active: root.highlight

        anchors {
            top: parent.top
            bottom: parent.bottom
            horizontalCenter: root.atRight ? parent.right : parent.left
        }

        width: VLCStyle.resizeHandleBorder
        color: theme.accent

        opacity: active ? 1 : 0
        visible: active || opacity > 0

        Behavior on opacity {
            OpacityAnimator {
                duration: VLCStyle.duration_veryLong
                easing.type: Easing.InOutSine
            }
        }
    }
}
