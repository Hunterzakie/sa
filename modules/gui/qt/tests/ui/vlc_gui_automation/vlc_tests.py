#!/usr/bin/env python3
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

import unittest, argparse, logging, platform, sys
from pathlib import Path
from .vlc_utils import get_vlc_actions, apply_vlc_data, launch_vlc
from .status import find_port, HttpStatus

if platform.system() == "Windows":
    from .windows.utils import get_driver_utils, cleanup, init_driver, setup_sandbox, revert_sandbox, get_music_dir, get_videos_dir, customize_toolbar
else:
    from .linux.utils import get_driver_utils, cleanup, init_driver, setup_sandbox, revert_sandbox, get_music_dir, get_videos_dir, customize_toolbar

VLC_PATH: str = ""

class VLCTests(unittest.TestCase):
    medialib_paths: list[str] | None = None
    video_list: list[str] = []
    music_list: list[str] = []
    vlc_args: list[str] = ["-A", "adummy", "--no-qt-privacy-ask", "--extraintf=http", "--http-password=vlc"]
    controlbar = None 

    def setUp(self):
        PREFIX_VIDEOS = Path("media_samples/video/")
        PREFIX_MUSIC = Path("media_samples/music/")

        try:
            self.tmp_dir = setup_sandbox()
            self.http_port = find_port()
            self.status = HttpStatus(self.http_port)
            self._apply_controlbar()

            apply_vlc_data(
                video_list=[Path(PREFIX_VIDEOS, video) for video in self.video_list],
                music_list=[Path(PREFIX_MUSIC, music) for music in self.music_list],
                vlc_path=VLC_PATH,
                medialib_paths=self.medialib_paths
            )
            self.vlc_process = launch_vlc(VLC_PATH, self.vlc_args + [f"--http-port={self.http_port}"])
            self.driver = init_driver(self.vlc_process.pid)
        except Exception:
            self.tearDown()
            raise
        self.utils = get_driver_utils(self.driver)
        self.vlc = get_vlc_actions(self.utils, self)

    def _apply_controlbar(self):
        method = getattr(self, self._testMethodName, None)
        data = getattr(method, "controlbar", None)
        if data is not None:
            self.controlbar = data
        if self.controlbar is not None and customize_toolbar is not None:
            customize_toolbar(self.controlbar)

    def tearDown(self):
        cleanup(self.vlc_process, self.driver)
        revert_sandbox(self.tmp_dir)
    
    def assertTimeout(self, condition, timeout=5):
        self.assertTrue(self.utils.wait_until(condition, timeout=timeout))

    def _setup_two_tracks(self):
        self.status.play_file(str(Path(get_music_dir(), "track.mp3")))
        self.assertTimeout(lambda: self.status.get_state() == "playing")
        track2 = str(Path(get_music_dir(), "track2.mp3"))
        self.status.send_command("in_enqueue", input=track2)

    def _open_music(self):
        self.utils.wait_until(
            lambda: self.status.get_state() is not None, timeout=5,
        )
        self.status.play_file(str(Path(get_music_dir(), "track.mp3")))

    def _open_video(self):
        self.utils.wait_until(
            lambda: self.status.get_state() is not None, timeout=5,
        )
        self.status.play_file(str(Path(get_videos_dir(), "anamorph.mkv")))

parser = argparse.ArgumentParser(description="Automated VLC tests")
parser.add_argument(
    "-p", "--path",
    type = str,
    required = True,
    help = "Path to VLC executable (e.g. C:/Program Files/VideoLAN/VLC/vlc.exe)"
)

args, remaining_args = parser.parse_known_args()

VLC_PATH = args.path

def run_tests():
    if platform.system() != "Windows" and platform.system() != "Linux":
        raise Exception("This testing framework only supports Windows and Linux platforms.")

    logging.basicConfig(level=logging.DEBUG, filename="testing.log")

    TESTS_DIR = str(Path("tests"))

    # Make test modules importable by name (e.g. test_basics.TestFoo.test_bar)
    sys.path.insert(0, TESTS_DIR)

    loader = unittest.TestLoader()
    if remaining_args:
        suite = loader.loadTestsFromName(remaining_args[0])
    else:
        suite = loader.discover(TESTS_DIR)
    
    result = unittest.TextTestRunner(verbosity=2).run(suite)
    if not result.wasSuccessful():
        exit(1)

    