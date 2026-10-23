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
import VLC.MainInterface
import VLC.MediaLibrary

import VLC.Style
import VLC.Widgets as Widgets

Widgets.PageExt {
    id: root

    title: qsTr("Tracks")

    hasGridListMode: false
    isSearchable: tracklistdisplay_id.isSearchable

    property alias model: tracklistdisplay_id.model
    property alias selectionModel: tracklistdisplay_id.selectionModel

    property var sortModel: [
        { text: qsTr("Title"),    criteria: "title"},
        { text: qsTr("Album"),    criteria: "album_title" },
        { text: qsTr("Artist"),   criteria: "main_artist" },
        { text: qsTr("Duration"), criteria: "duration" },
        { text: qsTr("Track"),    criteria: "track_number" },
        { text: qsTr("Disc"),     criteria: "disc_number" }
    ]

    function setCurrentItemFocus(reason) {
        tracklistdisplay_id.setCurrentItemFocus(reason);
    }

    function positionContentAtBeginning() {
        tracklistdisplay_id.positionViewAtBeginningAnimated()
    }

    readonly property list<Item> actions: [
        Widgets.ActionButtonPrimary {
            id: playButton

            visible: tracklistdisplay_id.model.count > 0

            width: height
            radius: width / 2

            anchors.verticalCenter: parent.verticalCenter

            iconTxt: VLCIcons.play_filled
            text: qsTr("Play all")
            showText: false
            iconSize: MainCtx.dp(20, VLCStyle.scale)

            Navigation.parentItem: root.header.actionsFocusScopeRow
            Navigation.rightItem: enqueueButton

            onClicked: tracklistdisplay_id.model.playAll()
        },
        Widgets.ActionButtonOverlay {
            id: enqueueButton

            visible: tracklistdisplay_id.model.count > 0

            focus: true

            anchors.verticalCenter: parent.verticalCenter

            width: VLCStyle.bannerButton_width
            height: VLCStyle.bannerButton_height
            padding: VLCStyle.margin_xxxsmall
            iconSize: MainCtx.dp(20, VLCStyle.scale)

            iconTxt: VLCIcons.enqueue
            text: qsTr("Enqueue all")
            showText: false

            Navigation.parentItem: root.header.actionsFocusScopeRow
            Navigation.leftItem: playButton

            onClicked: tracklistdisplay_id.model.enqueueAll()
        }
    ]

    Binding {
        target: root.header.actionsFocusScopeRow
        property: "children"
        value: root.actions
    }

    FocusScope {

        anchors.fill: parent

        focus: true

        // Aliases

        property alias leftPadding: tracklistdisplay_id.leftPadding
        property alias rightPadding: tracklistdisplay_id.rightPadding

        MusicTrackListDisplay {
            id: tracklistdisplay_id

            anchors.fill: parent

            visible: model.count > 0
            focus: model.count > 0

            searchPattern: root.search.pattern
            sortOrder: root.sort.order
            sortCriteria: root.sort.criteria

            Navigation.parentItem: root
            Navigation.upItem: root.header

            Navigation.cancelAction: function() {
                if (tracklistdisplay_id.currentIndex <= 0)
                    root.Navigation.defaultNavigationCancel()
                else
                    tracklistdisplay_id.currentIndex = 0;
            }


            displayMarginBeginning: root.displayMarginBeginning
            displayMarginEnd: root.displayMarginEnd

            fadingEdge.enableEndFade: root.enableEndFade
            fadingEdge.enableBeginningFade: root.enableBeginningFade
        }

        Widgets.EmptyLabelButton {
            anchors.centerIn: parent
            visible: !tracklistdisplay_id.model.loading && (tracklistdisplay_id.model.count <= 0)
            focus: visible
            text: qsTr("No tracks found\nPlease try adding sources")
            Navigation.parentItem: root
            cover: VLCStyle.noArtAlbumCover
        }
    }
}
