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
import QtQml.Models


import VLC.MainInterface
import VLC.Util
import VLC.Widgets as Widgets
import VLC.Style
import VLC.Playlist
import VLC.Network

FocusScope {
    id: root

    // Properties

    //behave like a Page
    property var pagePrefix: []

    readonly property bool hasGridListMode: false
    readonly property bool isSearchable: (_urlListDisplay && _urlListDisplay.isSearchable)

    property int leftPadding: 0
    property int rightPadding: 0

    property int displayMarginEnd: 0

    property bool enableBeginningFade: true
    property bool enableEndFade: true

    property Item _urlListDisplay // Can not use type `UrlListDisplay` because `MediaLibrary` module is not imported

    //---------------------------------------------------------------------------------------------
    // Functions
    //---------------------------------------------------------------------------------------------

    function setCurrentItemFocus(reason) {
        searchField.forceActiveFocus(reason);
    }

    //---------------------------------------------------------------------------------------------
    // Childs
    //---------------------------------------------------------------------------------------------

    Column {
        id: column
        anchors.fill: parent

        FocusScope {
            id: searchFieldContainer

            width: root.width
            height: searchField.height + VLCStyle.margin_normal * 2
            focus: true

            Navigation.parentItem:  root
            Navigation.downItem: root._urlListDisplay

            Widgets.TextFieldExt {
                id: searchField

                focus: true
                anchors.centerIn: parent
                height: VLCStyle.dp(32, VLCStyle.scale)
                width: root.width * .6
                placeholderText: qsTr("Paste or write the URL here")
                selectByMouse: true

                onAccepted: {
                    if (root._urlListDisplay)
                        root._urlListDisplay.model.addAndPlay(text)
                    else
                        MainPlaylistController.append([text], true)
                }

                Keys.priority: Keys.AfterItem
                Keys.onPressed: (event) => searchFieldContainer.Navigation.defaultKeyAction(event)

                //ideally we should use Keys.onShortcutOverride but it doesn't
                //work with TextField before 5.13 see QTBUG-68711
                onActiveFocusChanged: {
                    if (activeFocus)
                        MainCtx.useGlobalShortcuts = false
                    else
                        MainCtx.useGlobalShortcuts = true
                }
            }
        }

        Component.onCompleted: {
            if (MainCtx.mediaLibraryAvailable) {
                const component = MainCtx.createComponent('VLC.MediaLibrary', 'UrlListDisplay')
                root._urlListDisplay = component.incubateObject(column, { 'width': Qt.binding(() => column.width),
                                                                          'height': Qt.binding(() => column.height - searchFieldContainer.height),

                                                                          'leftPadding': Qt.binding(() => root.leftPadding),
                                                                          'rightPadding': Qt.binding(() => root.rightPadding),

                                                                          'displayMarginEnd': Qt.binding(() => root.displayMarginEnd),

                                                                          'fadingEdge.enableBeginningFade': Qt.binding(() => root.enableBeginningFade),
                                                                          'fadingEdge.enableEndFade': Qt.binding(() => root.enableEndFade),

                                                                          'Navigation.upItem': searchField,
                                                                          'Navigation.parentItem': root,

                                                                          'searchPattern': Qt.binding(() => MainCtx.search.pattern) },
                                                                        1 /* QQmlIncubator::AsynchronousIfNested */)
            }
        }
    }
}
