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
import QtQml.Models

import VLC.MainInterface
import VLC.Network
import VLC.Widgets as Widgets

Widgets.PageLoader {
    id: root

    property int displayMarginBeginning: 0
    property int displayMarginEnd: 0

    property bool enableBeginningFade: true
    property bool enableEndFade: true

    pageModel: [{
            name: "browse",
            default: true,
            displayText: qsTr("Browse"),
            url: "qrc:///qt/qml/VLC/Network/BrowseDisplay.qml"
        }, {
            name: "services",
            displayText: qsTr("Services"),
            url: "qrc:///qt/qml/VLC/Network/ServicesHomeDisplay.qml"
        }, {
            name: "url",
            displayText: qsTr("URL"),
            url: "qrc:///qt/qml/VLC/Network/DiscoverUrlDisplay.qml"
        }
    ]

    property ListModel tabModel: ListModel {
        Component.onCompleted: {
            pageModel.forEach(function(entry) {
                append({
                    displayText: entry.displayText,
                    name: entry.name,
                })
            })
        }
    }

    localMenuDelegate: currentItem?.localMenuDelegate ?? menuDelegate

    Accessible.role: Accessible.Client
    Accessible.name: qsTr("Network sources view")

    onCurrentItemChanged: {
        if (!currentItem)
            return

        if (currentItem.displayMarginBeginning !== undefined)
            currentItem.displayMarginBeginning = Qt.binding(() => { return root.displayMarginBeginning })

        if (currentItem.displayMarginEnd !== undefined)
            currentItem.displayMarginEnd = Qt.binding(() => { return root.displayMarginEnd })

        if (currentItem.enableBeginningFade !== undefined)
            currentItem.enableBeginningFade = Qt.binding(() => { return root.enableBeginningFade })

        if (currentItem.enableEndFade !== undefined)
            currentItem.enableEndFade = Qt.binding(() => { return root.enableEndFade })
    }

    function loadIndex(index) {
        const pageName = root.pageModel[index].name
        if (root.isDefaulLoadedForPath([pageName]))
            return

        History.push([...root.pagePrefix, pageName])
    }

    Component {
        id: menuDelegate

        Widgets.LocalTabBar {
            currentView: root.pageName
            model: root.tabModel

            onClicked: (index) => root.loadIndex(index)
        }
    }
}
