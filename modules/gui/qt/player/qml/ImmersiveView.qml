/*****************************************************************************
 * Copyright (C) 2025 VLC authors and VideoLAN
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
import QtQuick.Window

import VLC.MainInterface
import VLC.Style
import VLC.Widgets as Widgets
import VLC.PlayerControls

MinimalView {
    id: root

    component ImmersiveModeToggleButton : Widgets.IconButton {
        text: VLCIcons.pip

        description: qsTr("Immersive mode toggle")

        // Immersive mode is not supported on Wayland because:
        // - Window can not get hidden while video output is present, because interface window
        //   surface is destroyed before Qt 6.9.0.
        // - Always on top does not work.
        visible: MainCtx.hasEmbededVideo

        checked: MainCtx.immersiveMode

        onClicked: {
            MainCtx.immersiveMode = !checked
        }
    }

    ControlBar {
        id: controlBar

        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right

        hoverEnabled: true

        rightPadding: VLCStyle.applicationHorizontalMargin
        leftPadding: VLCStyle.applicationHorizontalMargin
        bottomPadding: VLCStyle.applicationVerticalMargin + VLCStyle.margin_xsmall

        textPosition: ControlBar.TimeTextPosition.LeftRightSlider // to save space

        identifier: PlayerControlbarModel.Immersiveplayer

        visible: opacity > 0.0

        opacity: (root._csdOnVideo || hovered) ? 1.0 : 0.0

        background.opacity: MainCtx.pinOpacity

        Behavior on opacity {
            NumberAnimation {
                //id: animation
                easing.type: Easing.OutSine
                duration: VLCStyle.duration_long
            }
        }
    }

    Component {
        id: immersiveModeToggleButtonComponent

        ImmersiveModeToggleButton {
            id: immersiveModeToggleButton

            anchors.verticalCenter: parent.verticalCenter
            anchors.right: csdDecorations.visible ? csdDecorations.left : parent.right
            anchors.rightMargin: csdDecorations.visible ? undefined : y

            Binding on visible {
                when: !(root._csdOnVideo || immersiveModeToggleButton.hovered)
                value: false
            }

            onHoveredChanged: {
                if (hovered) {
                    root.showControls()
                } else {
                    root.transientShowControls()
                }
            }
        }
    }

    DragHandler {
        target: null
        enabled: (MainCtx.intfMainWindow?.visibility === Window.Windowed)
        grabPermissions: TapHandler.CanTakeOverFromAnything
        onActiveChanged: {
            if (active) {
                MainCtx.intfMainWindow.startSystemMove();
            }
        }
    }

    Component.onCompleted: {
        // `videoSurface.activeFocusOnTab: true` does not work because
        // it is marked with REVISION(2, 1):
        videoSurface.activeFocusOnTab = true

        // Immersive mode is not supported on Wayland because:
        // - Window can not get hidden while video output is present.
        // - Always on top does not work.
        if (!Qt.platform.pluginName.startsWith("wayland"))
            immersiveModeToggleButtonComponent.incubateObject(csdDecorations?.parent, {}, 1 /* QQmlIncubator::AsynchronousIfNested */)

        root._oldWindowSize = Qt.size(MainCtx.intfMainWindow.width,
                                      MainCtx.intfMainWindow.height)

        const size = MainCtx.settingValue(_windowSizeKey,
                                          Qt.size(854, 480) /* Default size */)

        // NOTE: `QWindow::resize()` is not invokable.
        Qt.callLater(MainCtx.resizeWindow, MainCtx.intfMainWindow, size)
    }

    Component.onDestruction: {
        MainCtx.setSettingValue(_windowSizeKey, Qt.size(MainCtx.intfMainWindow.width,
                                                        MainCtx.intfMainWindow.height))

        if (_oldWindowSize.width > 0 && _oldWindowSize.height > 0) {
            // This should not be delayed:
            MainCtx.resizeWindow(MainCtx.intfMainWindow, _oldWindowSize)
        }
    }

    property size _oldWindowSize
    readonly property string _windowSizeKey: "ImmersiveView/windowSize"
}
