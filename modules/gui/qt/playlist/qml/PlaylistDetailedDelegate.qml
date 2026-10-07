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

import VLC.Playlist
import VLC.Widgets as Widgets
import VLC.Style

PlaylistDelegate {
    id: root

    property real contentTextHorizontalPadding

    // Set width to 0.0 to make the sections invisible
    property real titleWidth: VLCStyle.dp(256, VLCStyle.scale)
    property real durationWidth
    property real artistWidth
    property real genreWidth
    property real albumWidth
    property real urlWidth
    property real trackNumberWidth
    property real descriptionWidth
    property real trackIdWidth
    property real ratingWidth
    property real discNumberWidth
    property real dateWidth

    contentItem: Row {
        // We currently do not provide full fledged tool tip in detailed delegate,
        // but section specific tool tip is still available when hovered by mouse.

        // property string toolTipText: "%1%2%3%4%5%6%7%8%9%10%11%12".arg((titleLabel.implicitWidth > titleLabel.width) ? titleLabel.text + '\n' : "")
        //                                                           .arg((durationLabel.implicitWidth > durationLabel.width) ? durationLabel.text + '\n' : "")
        //                                                           .arg((artistLabel.implicitWidth > artistLabel.width) ? artistLabel.text + '\n' : "")
        //                                                           .arg((genreLabel.implicitWidth > genreLabel.width) ? genreLabel.text + '\n' : "")
        //                                                           .arg((albumLabel.implicitWidth > albumLabel.width) ? albumLabel.text + '\n' : "")
        //                                                           .arg((trackNumberLabel.implicitWidth > trackNumberLabel.width) ? trackNumberLabel.text + '\n' : "")
        //                                                           .arg((descriptionLabel.implicitWidth > descriptionLabel.width) ? descriptionLabel.text + '\n' : "")
        //                                                           .arg((urlLabel.implicitWidth > urlLabel.width) ? urlLabel.text + '\n' : "")
        //                                                           .arg((trackIdLabel.implicitWidth > trackIdLabel.width) ? trackIdLabel.text + '\n' : "")
        //                                                           .arg((ratingLabel.implicitWidth > ratingLabel.width) ? ratingLabel.text + '\n' : "")
        //                                                           .arg((discNumberLabel.implicitWidth > discNumberLabel.width) ? discNumberLabel.text + '\n' : "")
        //                                                           .arg((dateLabel.implicitWidth > dateLabel.width) ? dateLabel.text : "")

        property bool inhibitToolTip: true // (toolTipText.length <= 0)

        component DefaultLabel : Widgets.ListLabel {
            leftPadding: root.contentTextHorizontalPadding
            rightPadding: root.contentTextHorizontalPadding

            visible: (width > 0.0)

            font.weight: root.isCurrent ? Font.Bold : Font.DemiBold

            color: root.colorContext.fg.primary
        }

        DefaultLabel {
            id: titleLabel

            width: root.titleWidth
            text: root.title || qsTr("Unknown Title")
        }

        DefaultLabel {
            id: durationLabel

            width: root.durationWidth
            text: root.duration.formatHMS()
        }

        DefaultLabel {
            id: artistLabel

            width: root.artistWidth
            text: root.artist
        }

        DefaultLabel {
            id: genreLabel

            width: root.genreWidth
            text: root.genre
        }

        DefaultLabel {
            id: albumLabel

            width: root.albumWidth
            text: root.album
        }

        DefaultLabel {
            id: trackNumberLabel

            width: root.trackNumberWidth
            text: root.trackNumber
        }

        DefaultLabel {
            id: descriptionLabel

            width: root.descriptionWidth
            text: root.description
        }

        DefaultLabel {
            id: urlLabel

            width: root.urlWidth
            text: root.url
        }

        DefaultLabel {
            id: trackIdLabel

            width: root.trackIdWidth
            text: root.trackId
        }

        DefaultLabel {
            id: ratingLabel

            width: root.ratingWidth
            text: root.rating
        }

        DefaultLabel {
            id: discNumberLabel

            width: root.discNumberWidth
            text: root.discNumber
        }

        DefaultLabel {
            id: dateLabel

            width: root.dateWidth
            text: root.date
        }
    }
}
