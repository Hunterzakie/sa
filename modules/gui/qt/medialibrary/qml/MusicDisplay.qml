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
import QtQuick.Layouts

import VLC.MainInterface
import VLC.MediaLibrary

import VLC.Widgets as Widgets
import VLC.Style


Widgets.PageLoader {
    id: root

    Accessible.role: Accessible.Client
    Accessible.name: qsTr("Music view")

    pageModel: [{
            displayText: qsTr("Artists"),
            name: "artists",
            default: true,
            component: { module: 'VLC.MediaLibrary', type: 'MusicArtistsDisplay' }
        }, {
            displayText: qsTr("Albums"),
            name: "albums",
            component: { module: 'VLC.MediaLibrary', type: 'MusicAlbumsDisplay' }
        }, {
            displayText: qsTr("Tracks"),
            name: "tracks" ,
            component: { module: 'VLC.MediaLibrary', type: 'MusicTracksDisplay' }
        }, {
            displayText: qsTr("Genres"),
            name: "genres" ,
            component: { module: 'VLC.MediaLibrary', type: 'MusicGenresDisplay' }
        }, {
            displayText: qsTr("Playlists"),
            name: "playlists" ,
            component: { module: 'VLC.MediaLibrary', type: 'MusicPlaylistsDisplay' }
        }
    ]

    property int displayMarginBeginning: 0
    property int displayMarginEnd: 0

    property bool enableBeginningFade: true
    property bool enableEndFade: true

    onCurrentItemChanged: {
        if (currentItem) {
            if (currentItem.displayMarginBeginning !== undefined)
                currentItem.displayMarginBeginning = Qt.binding(() => { return root.displayMarginBeginning })

            if (currentItem.displayMarginEnd !== undefined)
                currentItem.displayMarginEnd = Qt.binding(() => { return root.displayMarginEnd })

            if (currentItem.enableBeginningFade !== undefined)
                currentItem.enableBeginningFade = Qt.binding(() => { return root.enableBeginningFade })

            if (currentItem.enableEndFade !== undefined)
                currentItem.enableEndFade = Qt.binding(() => { return root.enableEndFade })
        }
    }

    function loadIndex(index) {
        const pageName = root.pageModel[index].name
        if (root.isDefaulLoadedForPath([pageName]))
            return
        History.push([...root.pagePrefix, pageName])
    }

    property ListModel tabModel: ListModel {
        Component.onCompleted: {
            pageModel.forEach(function(e) {
                append({
                           displayText: e.displayText,
                           name: e.name,
                       })
            })
        }
    }

    localMenuDelegate: Widgets.LocalTabBar {
        currentView: root.pageName
        model: tabModel

        onClicked: (index) => {
            root.loadIndex(index)
        }
    }
}
