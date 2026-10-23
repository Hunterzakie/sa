# Copyright © 2026 VideoLabs, VLC authors and VideoLAN
#
# Authors: Wassim Lalaoui <wassim@videolabs.io>
#
# This program is free software; you can redistribute it and/or modify it
# under the terms of the GNU Lesser General Public License as published by
# the Free Software Foundation; either version 2.1 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
# GNU Lesser General Public License for more details.
#
# You should have received a copy of the GNU Lesser General Public License
# along with this program; if not, write to the Free Software Foundation,
# Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.

from enum import Enum, auto

class Keys(Enum):
    TAB = auto()
    RETURN = auto()
    ENTER = auto()
    ESCAPE = auto()
    SPACE = auto()
    BACKSPACE = auto()
    DELETE = auto()
    CONTROL = auto()
    SHIFT = auto()
    ALT = auto()
    ARROW_UP = auto()
    ARROW_DOWN = auto()
    ARROW_LEFT = auto()
    ARROW_RIGHT = auto()
    MENU = auto()
    F1 = auto()
    F2 = auto()
    F3 = auto()
    F4 = auto()
    F5 = auto()
    F6 = auto()
    F7 = auto()
    F8 = auto()
    F9 = auto()
    F10 = auto()
    F11 = auto()
    F12 = auto()
    PLUS = auto()
    MINUS = auto()
    EQUAL = auto()

class ControlType(Enum):
    PLAY_BUTTON = 0
    STOP_BUTTON = 1
    OPEN_BUTTON = 2
    PREVIOUS_BUTTON = 3
    NEXT_BUTTON = 4
    SLOWER_BUTTON = 5
    FASTER_BUTTON = 6
    FULLSCREEN_BUTTON = 7
    EXTENDED_BUTTON = 8
    PLAYLIST_BUTTON = 9
    SNAPSHOT_BUTTON = 10
    RECORD_BUTTON = 11
    ATOB_BUTTON = 12
    FRAME_NEXT_BUTTON = 13
    SKIP_BACK_BUTTON = 14
    SKIP_FW_BUTTON = 15
    QUIT_BUTTON = 16
    RANDOM_BUTTON = 17
    LOOP_BUTTON = 18
    INFO_BUTTON = 19
    LANG_BUTTON = 20
    MENU_BUTTON = 21
    BACK_BUTTON = 22
    CHAPTER_PREVIOUS_BUTTON = 23
    CHAPTER_NEXT_BUTTON = 24
    BUTTON_MAX = 25
    PLAYER_SWITCH_BUTTON = 26
    ARTWORK_INFO = 27
    PLAYBACK_SPEED_BUTTON = 28
    HIGH_RESOLUTION_TIME_WIDGET = 29
    FRAME_PREV_BUTTON = 30
    RESERVED_0X1F = 0x1F
    SPLITTER = 0x20
    VOLUME = 0x21
    TELETEXT_BUTTONS = 0x22
    ASPECT_RATIO_COMBOBOX = 0x23
    DVD_MENUS_BUTTON = 0x24
    REVERSE_BUTTON = 0x25
    BOOKMARK_BUTTON = 0x26
    RENDERER_BUTTON = 0x27
    NAVIGATION_BUTTONS = 0x28
    PROGRAM_BUTTON = 0x29
    NAVIGATION_BOX = 0x2A
    RESERVED_0X2B = 0x2B
    RESERVED_0X2C = 0x2C
    RESERVED_0X2D = 0x2D
    RESERVED_0X2E = 0x2E
    RESERVED_0X2F = 0x2F
    RESERVED_0X30 = 0x30
    RESERVED_0X31 = 0x31
    RESERVED_0X32 = 0x32
    RESERVED_0X33 = 0x33
    RESERVED_0X34 = 0x34
    RESERVED_0X35 = 0x35
    RESERVED_0X36 = 0x36
    RESERVED_0X37 = 0x37
    RESERVED_0X38 = 0x38
    RESERVED_0X39 = 0x39
    RESERVED_0X3A = 0x3A
    RESERVED_0X3B = 0x3B
    RESERVED_0X3C = 0x3C
    RESERVED_0X3D = 0x3D
    RESERVED_0X3E = 0x3E
    RESERVED_0X3F = 0x3F
    WIDGET_SPACER = 0x40
    WIDGET_SPACER_EXTEND = 0x41
    WIDGET_MAX = 0x42