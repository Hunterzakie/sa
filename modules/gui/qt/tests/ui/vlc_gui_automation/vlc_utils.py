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

"""
High-level VLC automation utilities.
These functions wrap lower-level test_utils functions to provide 
convenient, readable operations for VLC GUI testing.
"""
import shutil, platform, logging, subprocess
from pathlib import Path
from os import environ, getenv
from .utils import BaseDriverUtils
from .vlc import VLCActions

if platform.system() == "Windows":
    from .windows.utils import get_videos_dir, get_music_dir
else:
    from .linux.utils import get_videos_dir, get_music_dir

def get_vlc_actions(utils: BaseDriverUtils, test_case) -> VLCActions:
    return VLCActions(utils, test_case)

def apply_vlc_data(video_list, music_list, vlc_path, medialib_paths):
    """
    Applies video and music files to the user's Videos and Music folders respectively.
    Also applies the media library database if provided.
    """

    for video in video_list:
        dest_videos = get_videos_dir()
        if Path(video).exists():
            shutil.copy2(video, dest_videos)
            logging.info(f"Copied video: {video} → {dest_videos}")
        else:
            raise FileNotFoundError(f"Video file not found: {video}")

    for music in music_list:
        dest_music = get_music_dir()
        if Path(music).exists():
            shutil.copy2(music, dest_music)
            logging.info(f"Copied music: {music} → {dest_music}")
        else:
            raise FileNotFoundError(f"Music file not found: {music}")

    if medialib_paths is None:
        medialib_paths = [str(get_videos_dir()), str(get_music_dir())] # Default medialib paths
    if medialib_paths:
        if not all(Path(path).exists() for path in medialib_paths):
            raise FileNotFoundError(f"One or more media library database paths not found: {medialib_paths}")
        
        # medialib paths separated by ,
        medialib_paths_str = ",".join(str(path) for path in medialib_paths)

        try:
            result = subprocess.run([vlc_path,
                            "-I", "medialib_gen",
                            "--scan-folders", medialib_paths_str,
                            "-A", "adummy"],
                            timeout=500, capture_output=True, text=True, check=True)
        except subprocess.TimeoutExpired:
            logging.critical("Media library database generation timed out")
            raise RuntimeError("Media library database generation timed out")
        except subprocess.CalledProcessError as e:
            logging.critical(f"Media library database generation failed with the code {e.returncode}")
            raise RuntimeError(f"Media library database generation failed with the code {e.returncode}")
        
        logging.debug(f"Media library generation stdout: {result.stdout}")
        logging.debug(f"Media library generation stderr: {result.stderr}")
        logging.info(f"Applied media library databases from: {medialib_paths_str}")

def launch_vlc(vlc_path, vlc_args):
    try:
        env = environ.copy()
        env["QT_LINUX_ACCESSIBILITY_ALWAYS_ON"] = "1" # Only needed for Linux
        logging.info(f"Launching VLC from {vlc_path} with args: {vlc_args}")
        with open("vlc.log", "a") as vlc_log:
            vlc_process = subprocess.Popen([vlc_path] + vlc_args, env=env, stdout=vlc_log, stderr=vlc_log)
    except FileNotFoundError as e:
        logging.critical(f"Path not found: {vlc_path}. Exception: {e}")
        raise
    except OSError as e:
        logging.critical(f"Failed to launch VLC from {vlc_path}. Exception: {e}")
        raise
    return vlc_process