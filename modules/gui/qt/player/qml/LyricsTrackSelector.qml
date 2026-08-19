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

import VLC.Player
import VLC.Style
import VLC.Widgets as Widgets

// Picks which lyrics track the pane displays. A media may carry one track per
// language, and per language the lyrics next to the chords or the stage
// directions its tags also declared.
Widgets.ComboBoxExt {
    id: root

    // Nothing to pick from when the media carries a single track
    readonly property bool available: Player.lyricsTracks.length > 1

    // implicit rather than plain width/height so the item also behaves when
    // handed to a Layout, which drives the geometry itself
    implicitWidth: VLCStyle.combobox_width_normal
    implicitHeight: VLCStyle.combobox_height_normal

    model: Player.lyricsTracks

    Accessible.name: qsTr("Lyrics track")

    // Lets the surrounding interface keep itself visible while the popup is
    // open, the same way the control bar widgets do
    signal requestLockUnlockAutoHide(bool lock)

    Connections {
        target: root.popup

        function onOpened() {
            root.requestLockUnlockAutoHide(true)
        }

        function onClosed() {
            root.requestLockUnlockAutoHide(false)
        }
    }

    // A plain `currentIndex:` binding would be dropped the first time the user
    // picks an entry, since the ComboBox assigns the property itself; a Binding
    // keeps re-asserting the model's choice afterwards.
    Binding {
        target: root
        property: "currentIndex"
        value: Player.lyricsTrack
        restoreMode: Binding.RestoreNone
    }

    onActivated: (index) => {
        Player.lyricsTrack = index
    }
}
