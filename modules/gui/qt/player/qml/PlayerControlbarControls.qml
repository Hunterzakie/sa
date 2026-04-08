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

pragma Singleton

import QtQml


import VLC.MainInterface
import VLC.PlayerControls
import VLC.Widgets as Widgets
import VLC.Style

QtObject {
    readonly property var controlList: [
        { id: ControlListModel.PLAY_BUTTON, type: "PlayButton", label: VLCIcons.play_filled, text: qsTr("Play") },
        { id: ControlListModel.STOP_BUTTON, type: "StopButton", label: VLCIcons.stop, text: qsTr("Stop") },
        { id: ControlListModel.OPEN_BUTTON, type: "OpenButton", label: VLCIcons.eject, text: qsTr("Open") },
        { id: ControlListModel.PREVIOUS_BUTTON, type: "PreviousButton", label: VLCIcons.previous, text: qsTr("Previous") },
        { id: ControlListModel.NEXT_BUTTON, type: "NextButton", label: VLCIcons.next, text: qsTr("Next") },
        { id: ControlListModel.SLOWER_BUTTON, type: "SlowerButton", label: VLCIcons.slower, text: qsTr("Slower") },
        { id: ControlListModel.FASTER_BUTTON, type: "FasterButton", label: VLCIcons.faster, text: qsTr("Faster") },
        { id: ControlListModel.FULLSCREEN_BUTTON, type: "FullscreenButton", label: VLCIcons.fullscreen, text: qsTr("Fullscreen") },
        { id: ControlListModel.EXTENDED_BUTTON, type: "ExtendedSettingsButton", label: VLCIcons.effect_filter, text: qsTr("Extended panel") },
        { id: ControlListModel.PLAYLIST_BUTTON, type: "PlaylistButton", label: VLCIcons.playlist, text: qsTr("Playlist") },
        { id: ControlListModel.SNAPSHOT_BUTTON, type: "SnapshotButton", label: VLCIcons.snapshot, text: qsTr("Snapshot") },
        { id: ControlListModel.RECORD_BUTTON, type: "RecordButton", label: VLCIcons.record, text: qsTr("Record") },
        { id: ControlListModel.ATOB_BUTTON, type: "AtoBButton", label: VLCIcons.atob, text: qsTr("A-B Loop") },
        { id: ControlListModel.FRAME_BUTTON, type: "FrameButton", label: VLCIcons.frame_by_frame, text: qsTr("Frame By Frame") },
        { id: ControlListModel.REVERSE_BUTTON, type: "ReverseButton", label: VLCIcons.play_reverse, text: qsTr("Trickplay Reverse") },
        { id: ControlListModel.SKIP_BACK_BUTTON, type: "SkipBackButton", label: VLCIcons.skip_back, text: qsTr("Step backward") },
        { id: ControlListModel.SKIP_FW_BUTTON, type: "SkipForwardButton", label: VLCIcons.skip_for, text: qsTr("Step forward") },
        { id: ControlListModel.QUIT_BUTTON, type: "QuitButton", label: VLCIcons.clear, text: qsTr("Quit") },
        { id: ControlListModel.RANDOM_BUTTON, type: "RandomButton", label: VLCIcons.shuffle, text: qsTr("Random") },
        { id: ControlListModel.LOOP_BUTTON, type: "LoopButton", label: VLCIcons.repeat_all, text: qsTr("Loop") },
        { id: ControlListModel.INFO_BUTTON, type: "InfoButton", label: VLCIcons.info, text: qsTr("Information") },
        { id: ControlListModel.LANG_BUTTON, type: "LangButton", label: VLCIcons.audiosub, text: qsTr("Open subtitles") },
        { id: ControlListModel.BOOKMARK_BUTTON, type: "BookmarkButton", label: VLCIcons.bookmark, text: qsTr("Bookmark Button") },
        { id: ControlListModel.CHAPTER_PREVIOUS_BUTTON, type: "ChapterPreviousButton", label: VLCIcons.dvd_prev, text: qsTr("Previous chapter") },
        { id: ControlListModel.CHAPTER_NEXT_BUTTON, type: "ChapterNextButton", label: VLCIcons.dvd_next, text: qsTr("Next chapter") },
        { id: ControlListModel.VOLUME, type: "VolumeWidget", label: VLCIcons.volume_high, text: qsTr("Volume Widget") },
        { id: ControlListModel.NAVIGATION_BOX, type: "NavigationBoxButton", label: VLCIcons.ic_fluent_arrow_move, text: qsTr("Navigation Box") },
        { id: ControlListModel.NAVIGATION_BUTTONS, type: "NavigationWidget", label: VLCIcons.dvd_menu, text: qsTr("Navigation") },
        { id: ControlListModel.DVD_MENUS_BUTTON, type: "DvdMenuButton", label: VLCIcons.dvd_menu, text: qsTr("DVD menus") },
        { id: ControlListModel.PROGRAM_BUTTON, type: "ProgramButton", label: VLCIcons.tv, text: qsTr("Program Button") },
        { id: ControlListModel.TELETEXT_BUTTONS, type: "TeletextButton", label: VLCIcons.tvtelx, text: qsTr("Teletext") },
        { id: ControlListModel.RENDERER_BUTTON, type: "RendererButton", label: VLCIcons.renderer, text: qsTr("Renderer Button") },
        { id: ControlListModel.ASPECT_RATIO_COMBOBOX, type: "AspectRatioWidget", label: VLCIcons.aspect_ratio, text: qsTr("Aspect Ratio") },
        { id: ControlListModel.WIDGET_SPACER, type: "SpacerWidget", label: VLCIcons.space, text: qsTr("Spacer") },
        { id: ControlListModel.WIDGET_SPACER_EXTEND, type: "ExpandingSpacerWidget", label: VLCIcons.space, text: qsTr("Expanding Spacer") },
        { id: ControlListModel.PLAYER_SWITCH_BUTTON, type: "PlayerSwitchButton", label: VLCIcons.fullscreen, text: qsTr("Switch Player") },
        { id: ControlListModel.ARTWORK_INFO, type: "ArtworkInfoWidget", label: VLCIcons.info, text: qsTr("Artwork Info") },
        { id: ControlListModel.PLAYBACK_SPEED_BUTTON, type: "PlaybackSpeedButton", label: "1x", text: qsTr("Playback Speed") },
        { id: ControlListModel.HIGH_RESOLUTION_TIME_WIDGET, type: "HighResolutionTimeWidget", label: VLCIcons.info, text: qsTr("High Resolution Time") }
    ]

    function control(id) {
        let entry = controlList.find( function(e) { return ( e.id === id ) } )

        let type
        if (entry === undefined) {
            console.warn("control delegate id " + id +  " doesn't exist")
            type = 'Fallback'
            entry = {}
        } else {
            type = entry.type
        }

        entry.component = MainCtx.createComponent('VLC.PlayerControls', type)

        return entry
    }
}
