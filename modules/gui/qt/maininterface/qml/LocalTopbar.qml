/*****************************************************************************
 * Copyright (C) 2019 VLC authors and VideoLAN
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
import QtQuick.Layouts
import QtQml.Models
import QtQuick.Window


import VLC.MainInterface
import VLC.Style
import VLC.Playlist
import VLC.Widgets as Widgets
import VLC.Menus as Menus
import VLC.Util

T.ToolBar {
    id: root

    property bool useAcrylic: true

    // For now, used for d&d functionality
    // Not strictly necessary to set
    property Item plListView

    property bool _showCSD: MainCtx.clientSideDecoration
        && !(MainCtx.intfMainWindow.visibility === Window.FullScreen)
        && !MainCtx.hasToolbarMenu

    //whether the navigation pannel is actually visible or not
    property alias navigationVisible: navigationVisibilty.checked
    property alias playqueueVisible: playlistBtn.checked

    signal toggleNavigationVisibility()
    signal togglePlayqueueVisibility()

    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitContentHeight + topPadding + bottomPadding)

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                             implicitContentWidth + leftPadding + rightPadding)

    leftPadding: implicitLeftPadding
    rightPadding: implicitRightPadding

    property real implicitLeftPadding: VLCStyle.margin_xsmall
    property real implicitRightPadding: _showCSD ? 0.0 : VLCStyle.margin_xsmall

    readonly property ColorContext colorContext: ColorContext {
        id: theme
        colorSet: ColorContext.Window
    }

    background:  Widgets.AcrylicBackground {
        enabled: root.useAcrylic
        tintColor: theme.bg.primary
    }

    contentItem: Item {

        implicitHeight: VLCStyle.globalToolbar_height
        implicitWidth: rowLayout.implicitWidth

        RowLayout {
            id: rowLayout

            anchors.fill: parent

            spacing: VLCStyle.margin_normal

            Navigation.parentItem: root

            Widgets.IconToolButton {
                id: navigationVisibilty

                focus: true

                text: VLCIcons.panel_left
                description: qsTr("Show navigation menu")

                font.pixelSize: VLCStyle.icon_banner
                Layout.preferredHeight: VLCStyle.bannerButton_height
                Layout.preferredWidth: VLCStyle.bannerButton_width

                onClicked: root.toggleNavigationVisibility()

                Navigation.parentItem: rowLayout
                Navigation.rightItem: historyBack
            }

            Widgets.IconToolButton {
                id: historyBack

                font.pixelSize: VLCStyle.icon_banner
                text: VLCIcons.back
                description: qsTr("Previous")

                Layout.preferredHeight: VLCStyle.bannerButton_height
                Layout.preferredWidth: VLCStyle.bannerButton_width

                onClicked: History.previous()
                enabled: !History.previousEmpty

                onEnabledChanged: {
                    if (historyBack.activeFocus)
                        navigationVisibilty.focus = true
                }

                Navigation.parentItem: rowLayout
                Navigation.leftItem: navigationVisibilty
                Navigation.rightItem: playlistBtn
            }

            Widgets.BannerCone {
                id: logo

                sourceSize.width: VLCStyle.bannerButton_height
                sourceSize.height: VLCStyle.bannerButton_height
                color: theme.accent
            }

            Widgets.CaptionLabel {
                Layout.fillWidth: true

                color: theme.fg.primary

                // Visible at all times, spacer with SSD (no text).
                text: {
                    if (!root._showCSD)
                        return ""

                    let pre
                    if (Window?.title?.length > 0) // `Window.title` is not available in Qt 6.2
                        pre = Window.title
                    else if (Window.window?.title.length > 0)
                        pre = Window.window.title
                    else if (MainCtx.intfMainWindow?.title.length > 0)
                        pre = MainCtx.intfMainWindow.title

                    if (pre)
                        return pre + " \u2014 " + Application.displayName // Akin to KWin
                    else
                        return Application.displayName
                }
            }

            Widgets.IconToolButton {
                id: playlistBtn

                font.pixelSize: VLCStyle.icon_banner
                text: VLCIcons.playlist
                description: qsTr("Playlist")
                Layout.preferredWidth: VLCStyle.bannerButton_width
                Layout.preferredHeight: VLCStyle.bannerButton_height
                //highlighted: MainCtx.playlistVisible

                Navigation.parentItem: rowLayout
                Navigation.rightItem: menuBtn
                Navigation.leftItem: historyBack

                onClicked:  root.togglePlayqueueVisibility()

                DropArea {
                    anchors.fill: parent

                    onContainsDragChanged: {
                        if (containsDrag) {
                            _timer.restart()

                            if (plListView)
                                MainCtx.setCursor(Qt.DragCopyCursor)
                        } else {
                            _timer.stop()

                            if (plListView)
                                MainCtx.restoreCursor()
                        }
                    }

                    onEntered: (drag) => {
                        if (root.plListView) {
                            console.assert(root.plListView.isDropAcceptableFunc)
                            console.assert(root.plListView.model)
                            if (root.plListView.isDropAcceptableFunc(drag, root.plListView.model.count)) {
                                drag.accept()
                            } else {
                                drag.accepted = false
                            }
                        } else {
                            drag.accepted = false
                        }
                    }

                    onDropped: (drop) => {
                        if (root.plListView) {
                            console.assert(plListView.acceptDropFunc)
                            root.plListView.acceptDropFunc(root.plListView.model.count, drop)
                        }
                    }

                    Timer {
                        id: _timer
                        interval: VLCStyle.duration_humanMoment

                        onTriggered: {
                            MainCtx.playqueuePanel.visible = true
                        }
                    }
                }
            }

            Widgets.IconToolButton {
                id: menuBtn

                visible: !MainCtx.hasToolbarMenu
                font.pixelSize: VLCStyle.icon_banner
                text: VLCIcons.more
                description: qsTr("Menu")
                Layout.preferredWidth: VLCStyle.bannerButton_width
                Layout.preferredHeight: VLCStyle.bannerButton_height
                checked: contextMenu.shown

                Navigation.parentItem: rowLayout
                Navigation.leftItem: playlistBtn

                onClicked: contextMenu.popup(this.mapToGlobal(0, height))

                Menus.QmlGlobalMenu {
                    id: contextMenu
                    ctx: MainCtx
                }
            }

            Loader {
                id: csdButtons

                visible: active
                height: VLCStyle.globalToolbar_height
                active: root._showCSD
                source: VLCStyle.palette.hasCSDImage
                          ? "qrc:///qt/qml/VLC/Widgets/CSDThemeButtonSet.qml"
                          : "qrc:///qt/qml/VLC/Widgets/CSDWindowButtonSet.qml"
            }
        }

        Loader {
            z: -1

            anchors.fill: parent
            active: root._showCSD
            sourceComponent: Widgets.CSDTitlebarTapNDrapHandler {
            }
        }
    }
}
