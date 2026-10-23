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

import tempfile, shutil, logging, time
from os import environ, getenv
from pathlib import Path
from .test_utils import LinuxDriverUtils
from ..exceptions import DriverError
from ..enums import ControlType
from dogtail.tree import root

def init_driver(vlc_pid=None):
    def wait_for_window(timeout=10):
        """
        Wait for VLC window to appear in dogtail's accessibility tree.
        Returns the VLC driver node or None if not found within timeout.
        """
        start_time = time.time()
        driver = None
        
        while driver is None and (time.time() - start_time) < timeout:
            if root is None:
                raise RuntimeError("Dogtail root is None (AT-SPI not ready)")
            driver = next(
                (a for a in root.children if "vlc" in a.name and a.children),
                None
            )
            if driver is None:
                time.sleep(0.2)  # Wait a bit before retrying
        
        if driver is None:
            raise TimeoutError(f"No vlc at-spi node with children found after {timeout} seconds")

        return driver

    try:
        driver = wait_for_window(timeout=10)
    except TimeoutError as e:
        logging.critical(f"Did not find a VLC at-spi node within the timeout period. Exception: {e}")
        raise DriverError("VLC at-spi node did not appear within the timeout period.") from e
    return driver

def setup_sandbox():
    tmp_dir = Path(tempfile.mkdtemp(prefix="vlc_"))

    logging.info("Tempfile created: " + str(tmp_dir))

    music_dir = tmp_dir / "music"
    videos_dir = tmp_dir / "videos"
    data_dir = tmp_dir / "data"
    config_dir = tmp_dir / "config"
    cache_dir = tmp_dir / "cache"

    music_dir.mkdir(parents=True, exist_ok=True)
    videos_dir.mkdir(parents=True, exist_ok=True)
    data_dir.mkdir(parents=True, exist_ok=True)
    config_dir.mkdir(parents=True, exist_ok=True)
    cache_dir.mkdir(parents=True, exist_ok=True)

    environ["XDG_MUSIC_DIR"] = str(music_dir)
    environ["XDG_VIDEOS_DIR"] = str(videos_dir)
    environ["XDG_DATA_HOME"] = str(data_dir)
    environ["XDG_CONFIG_HOME"] = str(config_dir)
    environ["XDG_CACHE_HOME"] = str(cache_dir)

    return tmp_dir

def revert_sandbox(tmp_dir: Path):
    """ On Linux, just cleans up the temp directory """
    if tmp_dir and tmp_dir.exists():
        shutil.rmtree(tmp_dir)

def cleanup(vlc_process, driver=None):
    if vlc_process is not None:
        vlc_process.terminate()
        vlc_process.wait()


def get_driver_utils(driver):
    return LinuxDriverUtils(driver)

def get_videos_dir() -> Path:
    """ Returns the user's Videos directory """
    xdg_videos_dir = getenv("XDG_VIDEOS_DIR")
    if not xdg_videos_dir:
        raise RuntimeError("XDG_VIDEOS_DIR environment variable was not set.")
    return Path(xdg_videos_dir)
    
def get_music_dir() -> Path:
    """Returns the user's Music directory"""
    xdg_music_dir = getenv("XDG_MUSIC_DIR")
    if not xdg_music_dir:
        raise RuntimeError("XDG_MUSIC_DIR environment variable was not set.")
    return Path(xdg_music_dir)
    
def get_cache_dir() -> Path:
    """ Returns cache directory """
    xdg_cache_home = getenv("XDG_CACHE_HOME")
    if not xdg_cache_home:
        raise RuntimeError("XDG_CACHE_HOME environment variable was not set.")
    return Path(xdg_cache_home)

def get_config_dir():
    """ Should only exist on Linux, on Windows this is merged with data_dir """
    xdg_config_home = getenv("XDG_CONFIG_HOME")
    if not xdg_config_home:
        raise RuntimeError("XDG_CONFIG_HOME environment variable was not set.")
    return Path(xdg_config_home)

def _cid(c):
    return c.value if isinstance(c, ControlType) else c

def customize_toolbar(items, profile_name="Test"):
    config_dir = Path(get_config_dir()) / "vlc"
    config_dir.mkdir(parents=True, exist_ok=True)
    config_file = config_dir / "vlc-qt-interface.conf"

    center_str = ",".join(str(_cid(c)) for c in items)
    model = "".join(f"${i}||{center_str}|" for i in range(3))

    content = (
        "[ControlbarProfileModel]\n"
        "SelectedProfile=0\n"
        "Profiles\\size=1\n"
        f"Profiles\\1\\Name={profile_name}\n"
        f"Profiles\\1\\Model=\"{model}\"\n"
    )

    with open(config_file, "w") as f:
        f.write(content)
    logging.info("Wrote controlbar profile to %s", config_file)

def get_data_dir() -> Path:
    """ Returns data directory """
    appdata = getenv("XDG_DATA_HOME")
    if not appdata:
        raise RuntimeError("XDG_DATA_HOME environment variable was not set.")
    return Path(appdata)
