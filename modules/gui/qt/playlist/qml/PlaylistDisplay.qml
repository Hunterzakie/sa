/*****************************************************************************
 * Copyright (C) 2026 VLC authors and VideoLAN
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
import QtQml
import QtQml.Models

import VLC.MainInterface
import VLC.Playlist
import VLC.Style

FocusScope {
    id: root

    property var pagePrefix: []
    property int initialIndex: -1

    property int displayMarginBeginning: 0
    property int displayMarginEnd: 0

    readonly property bool hasGridListMode: false
    readonly property bool isSearchable: false

    Accessible.role: Accessible.Client
    Accessible.name: qsTr("Playlist view")

    function setCurrentItemFocus(reason) {
        playlistPane.forceActiveFocus(reason)
    }

    function restoreCurrentIndex() {
        if (initialIndex < 0 || !playlistPane.listView || !playlistPane.selectionModel || playlistPane.model.count === 0)
            return

        const index = Math.min(initialIndex, playlistPane.model.count - 1)
        playlistPane.selectionModel.select(playlistPane.model.index(index, 0), ItemSelectionModel.ClearAndSelect)
        playlistPane.listView.currentIndex = index
        playlistPane.listView.positionViewAtIndex(index, ItemView.Contain)
    }

    Component.onCompleted: restoreCurrentIndex()

    onInitialIndexChanged: restoreCurrentIndex()

    PlaylistPane {
        id: playlistPane

        anchors.fill: parent
        anchors.leftMargin: VLCStyle.applicationHorizontalMargin
        anchors.rightMargin: VLCStyle.applicationHorizontalMargin
        anchors.topMargin: VLCStyle.margin_small
        anchors.bottomMargin: root.displayMarginEnd

        focus: true
        useAcrylic: false

        topPadding: VLCStyle.layoutTitle_top_padding
        bottomPadding: VLCStyle.margin_normal + Math.max(VLCStyle.applicationVerticalMargin - root.displayMarginEnd, 0)

        colorContext.palette: VLCStyle.palette

        Connections {
            target: playlistPane.listView

            function onCurrentIndexChanged() {
                if (History.viewProp)
                    History.viewProp.initialIndex = playlistPane.listView.currentIndex
            }
        }

        Connections {
            target: playlistPane.model

            function onRowsInserted() {
                root.restoreCurrentIndex()
            }

            function onModelReset() {
                root.restoreCurrentIndex()
            }
        }
    }
}
