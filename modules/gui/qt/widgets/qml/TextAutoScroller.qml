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

import VLC.Widgets as Widgets
import VLC.Style

Item {
    id: root

    // No need for additional clipping, when applicable:
    Binding on clip {
        when: !!fadingEdgeLoader.item?.implicitClipping
        value: false
    }

    readonly property alias scrolling: scrollAnimation.running

    // `label`: label to scroll, don't add horizontal anchors on it
    property Text label: undefined
    property bool forceScroll: false

    property bool enableFading: scrolling && visible
    // Not required to provide, but if you can deduce opaque background
    // color, it is beneficial to provide since it allows saving video
    // memory and retains sub-pixel font rendering. Also see
    // `FadingEdge::backgroundColor`:
    property color backgroundColor: "transparent"

    readonly property real requiredTextWidth: label.implicitWidth
    readonly property bool _needsToScroll: (label.width < requiredTextWidth)

    //Accessible
    Accessible.role: Accessible.StaticText
    Accessible.name: label.text

    onLabelChanged: {
        label.width = Qt.binding(function () { return Math.min(label.implicitWidth, root.width) })

        label.elide = Qt.binding(function () {
            return root.scrolling ? Text.ElideNone : Text.ElideRight
        })

        if (label.mutabilityGroup !== undefined)
            label.mutabilityGroup = Qt.binding(function() { return (root.label === label && root.scrolling) ? Item.DynamicMutabilityGroup
                                                                                                            : Item.AutoMutabilityGroup })

        label.Accessible.ignored = true
    }

    Loader {
        id: fadingEdgeLoader

        active: root.enableFading

        // This breaks batch rendering if background color is not opaque,
        // but it is already broken due to clipping. This is not really a
        // concern, since in item view cases only one delegate instance
        // should be using scrolling text at a time.
        sourceComponent: Widgets.FadingEdge {
            id: fadingEdge

            y: mappedPos.y
            width: root.width
            height: root.height

            visible: root.label && root.scrolling

            readonly property point mappedPos: root.label ? root.mapFromItem(root.label.parent, root.label.x, root.label.y)
                                                          : Qt.point(0.0, 0.0)

            sourceItem: root.label
            sourceX: -mappedPos.x

            backgroundColor: root.backgroundColor

            orientation: Qt.Horizontal

            fadeSize: VLCStyle.margin_normal
        }
    }

    SequentialAnimation {
        id: scrollAnimation

        running: root.forceScroll && root._needsToScroll
        loops: Animation.Infinite

        onStopped: {
            label.x = 0
        }

        PauseAnimation {
            duration: VLCStyle.duration_veryLong
        }

        SmoothedAnimation {
            target: label
            property: "x"
            from: 0
            to: label.width - root.requiredTextWidth

            maximumEasingTime: 0
            velocity: 20
        }

        PauseAnimation {
            duration: VLCStyle.duration_veryLong
        }

        PropertyAction {
            target: label
            property: "x"
            value: 0
        }
    }
}

