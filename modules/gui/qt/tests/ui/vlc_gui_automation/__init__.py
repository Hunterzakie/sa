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

from platform import system
from .decorators import windows_only, linux_only, disabled, controlbar
from .exceptions import ElementNotFoundError, PreconditionError, PostconditionError
from .enums import Keys, ControlType
from .vlc_tests import VLCTests, run_tests
from .status import find_port, HttpStatus
if system() == "Windows":
    from .windows.utils import get_music_dir, get_videos_dir
else:
    from .linux.utils import get_music_dir, get_videos_dir