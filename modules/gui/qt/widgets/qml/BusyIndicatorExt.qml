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
import QtQuick.Templates as T

import VLC.Widgets
import VLC.Style
import VLC.Util

T.BusyIndicator {
    id: control

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitContentHeight + topPadding + bottomPadding)

    padding: VLCStyle.margin_xsmall

    property color maskColor: "gray"
    property int animationDuration: VLCStyle.duration_veryLong
    property var backgroundColor

    component AnimatedCone : Item {
        id: item

        implicitWidth: VLCStyle.icon_large
        implicitHeight: VLCStyle.icon_large

        property alias animate: timer.running
        property alias animateDuration: timer.interval

        required property color maskColor

        property color coneAccentColor: "#FF8800"
        property color coneWhiteColor: "#FFFFFF"

        property var backgroundColor

        property int _current: 0

        Timer {
            id: timer

            interval: 250

            repeat: true

            onTriggered: {
                if (item._current === 5)
                    item._current = 0
                else
                    item._current++
            }
        }

        // We purposefully not have one `Image` and adjust the source, because we
        // want to make sure that the texture generated and uploaded to gpu is
        // reused.
        ColorizedCone {
            coneItem: item
            index: 0
            colorList: [item.maskColor,
                        item.maskColor,
                        item.maskColor,
                        item.maskColor,
                        item.maskColor]
        }

        ColorizedCone {
            coneItem: item
            index: 1
            colorList: [item.coneAccentColor,
                        item.maskColor,
                        item.maskColor,
                        item.maskColor,
                        item.maskColor]
        }

        ColorizedCone {
            coneItem: item
            index: 2
            colorList: [item.coneAccentColor,
                        item.coneWhiteColor,
                        item.maskColor,
                        item.maskColor,
                        item.maskColor]
        }

        ColorizedCone {
            coneItem: item
            index: 3
            colorList: [item.coneAccentColor,
                        item.coneWhiteColor,
                        item.coneAccentColor,
                        item.maskColor,
                        item.maskColor]
        }

        ColorizedCone {
            coneItem: item
            index: 4
            colorList: [item.coneAccentColor,
                        item.coneWhiteColor,
                        item.coneAccentColor,
                        item.coneWhiteColor,
                        item.maskColor]
        }

        ColorizedCone {
            coneItem: item
            index: 5
            colorList: [item.coneAccentColor,
                        item.coneWhiteColor,
                        item.coneAccentColor,
                        item.coneWhiteColor,
                        item.coneAccentColor]
        }
    }

    component DefaultAnimatedCone : AnimatedCone {
        required property BusyIndicatorExt root

        animate: root.running
        animateDuration: root.animationDuration

        backgroundColor: root.backgroundColor
        maskColor: root.maskColor

        opacity: root.running ? 1 : 0

        Behavior on opacity {
            OpacityAnimator {
                duration: VLCStyle.duration_short
            }
        }
    }

    contentItem: DefaultAnimatedCone {
        root: control
    }

    // This component is not supposed to be public, but
    // "Nested inline components are not supported"...
    component ColorizedCone : ScaledImage {
        anchors.fill: parent

        visible: (coneItem._current === index)

        required property var colorList
        required property int index

        required property AnimatedCone coneItem

        source: colorizedCone(colorList)

        function colorizedCone(list) : url {
            console.assert(list.length === 5)
            let builder = SVGColorImage.colorize("qrc:///misc/cone_segmented.svg").any({"#00FF00": list[0],
                                                                                        "#FF0000": list[1],
                                                                                        "#0000FF": list[2],
                                                                                        "#FF00FF": list[3],
                                                                                        "#000000": list[4]})
            if (!!coneItem.backgroundColor)
                builder = builder.background(coneItem.backgroundColor)

            return builder.uri()
        }
    }
}
