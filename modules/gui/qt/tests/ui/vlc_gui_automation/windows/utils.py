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

import tempfile, subprocess, shutil, logging, win32process, win32gui, time, atexit
from os import getenv, environ
from pathlib import Path
from .test_utils import WindowsDriverUtils
from ..exceptions import DriverError
from ..enums import ControlType
from appium import webdriver
from appium.options.windows import WindowsOptions
from appium.webdriver.appium_service import AppiumService

def init_driver(vlc_pid):
    def wait_for_window(pid, timeout=10, poll_interval=0.1):
        """
        Waits for a window that belongs to the given PID to appear and returns its handle
        """
        def find_windows():
            hwnds = []
            def callback(hwnd, _):
                _, found_pid = win32process.GetWindowThreadProcessId(hwnd)
                if found_pid == pid and win32gui.IsWindowVisible(hwnd):
                    hwnds.append(hwnd)
                return True
            win32gui.EnumWindows(callback, None)
            return hwnds

        start_time = time.time()
        while True:
            hwnds = find_windows()
            if hwnds:
                return hwnds[0]
            if time.time() - start_time > timeout:
                raise TimeoutError(f"No window found for the PID {pid} after {timeout} seconds")
            time.sleep(poll_interval)
    try:
        window_handle = wait_for_window(vlc_pid, timeout=30)
    except TimeoutError as e:
        logging.critical(f"Did not find a VLC window within the timeout period for the PID {vlc_pid}. Exception: {e}")
        raise DriverError("VLC window did not appear within the timeout period.") from e
    service = AppiumService()
    service.start(args=['--address', '127.0.0.1', '--port', '4723'], timeout_ms=20000)
    atexit.register(service.stop)
    options = WindowsOptions()
    options.set_capability("appTopLevelWindow", window_handle)
    options.set_capability("automationName", "NovaWindows")
    options.set_capability("platformName", "Windows")
    options.set_capability("newCommandTimeout", 300) # to avoid VLC from closing when reading videos
    driver = webdriver.Remote(command_executor='http://127.0.0.1:4723', options=options)
    if driver is None:
        logging.critical("Failed to create Appium Windows Driver.")
        raise DriverError("Failed to create Appium Windows Driver.")
    driver.implicitly_wait(10)
    return driver

def setup_sandbox():
    tmp_dir = Path(tempfile.mkdtemp(prefix="vlc_"))

    logging.info("Tempfile created: " + str(tmp_dir))
    
    vlc_appdata = get_data_dir() / "vlc"
    appdata_target = tmp_dir / "AppData"
    appdata_target.mkdir(parents=True, exist_ok=True)

    vlc_localappdata = get_cache_dir() / "vlc"
    localappdata_target = tmp_dir / "LocalAppData"
    localappdata_target.mkdir(parents=True, exist_ok=True)

    vlc_appdata_backup = vlc_appdata.with_name("vlc_backup")
    if vlc_appdata.exists():
        vlc_appdata.rename(vlc_appdata_backup)

    vlc_localappdata_backup = vlc_localappdata.with_name("vlc_backup")
    if vlc_localappdata.exists():
        vlc_localappdata.rename(vlc_localappdata_backup)

    # Because we can't overwrite APPDATA variable, we create a junction point from the original VLC AppData to our temp AppData
    try:
        subprocess.check_call([
            "cmd", "/C", "mklink", "/J", str(vlc_appdata), str(appdata_target)
        ])
        subprocess.check_call([
            "cmd", "/C", "mklink", "/J", str(vlc_localappdata), str(localappdata_target)
        ])
    except subprocess.CalledProcessError as e:
        if vlc_appdata_backup.exists():
            vlc_appdata_backup.rename(vlc_appdata)
        if vlc_localappdata_backup.exists():
            vlc_localappdata_backup.rename(vlc_localappdata)
        raise RuntimeError(f"Failed to create junction from '{vlc_appdata}' to '{appdata_target}'.") from e

    music_dir = tmp_dir / "music"
    videos_dir = tmp_dir / "videos"

    music_dir.mkdir(parents=True, exist_ok=True)
    videos_dir.mkdir(parents=True, exist_ok=True)

    environ["MUSIC_DIR"] = str(music_dir)
    environ["VIDEOS_DIR"] = str(videos_dir)
    return tmp_dir

def revert_sandbox(tmp_dir):
    if tmp_dir and tmp_dir.exists():

        vlc_appdata_junction = get_data_dir() / "vlc"
        vlc_appdata_backup = get_data_dir() / "vlc_backup"
        
        if vlc_appdata_junction.exists():
            vlc_appdata_junction.rmdir()

        if vlc_appdata_backup.exists():
            vlc_appdata_dir = vlc_appdata_backup.with_name("vlc")
            vlc_appdata_backup.rename(vlc_appdata_dir)

        vlc_localappdata_junction = get_cache_dir() / "vlc"
        vlc_localappdata_backup = get_cache_dir() / "vlc_backup"

        if vlc_localappdata_junction.exists():
            vlc_localappdata_junction.rmdir()
        
        if vlc_localappdata_backup.exists():
            vlc_localappdata_dir = vlc_localappdata_backup.with_name("vlc")
            vlc_localappdata_backup.rename(vlc_localappdata_dir)

        shutil.rmtree(tmp_dir)
    else:
        logging.warning("tmp_dir didnt exist during revert_sandbox even though it should.")

def cleanup(vlc_process, driver):
    if driver:
        driver.quit()

    if vlc_process is not None:
        vlc_process.terminate()
        vlc_process.wait()

    

def get_driver_utils(driver):
    return WindowsDriverUtils(driver)

def get_videos_dir() -> Path:
    """Returns Videos directory"""
    videos_dir = getenv("VIDEOS_DIR")
    if videos_dir is None:
        raise RuntimeError("VIDEOS_DIR environment variable was not set.")
    return Path(videos_dir)

def get_music_dir() -> Path:
    """Returns Music directory"""
    music_dir = getenv("MUSIC_DIR")
    if music_dir is None:
        raise RuntimeError("MUSIC_DIR environment variable was not set.")
    return Path(music_dir)

def get_data_dir() -> Path:
    """ Returns data directory """
    data_dir = getenv("APPDATA")
    if data_dir is None:
        raise RuntimeError("APPDATA environment variable was not set.")
    return Path(data_dir)

def get_cache_dir() -> Path:
    """Returns cache directory"""
    localappdata = getenv("LOCALAPPDATA")
    if not localappdata:
        raise EnvironmentError("LOCALAPPDATA environment variable is not set.")
    return Path(localappdata)

def get_config_dir():
    """ Should only exist on Linux, on Windows this is merged with data_dir so we return None"""
    return None

def _cid(c):
    return c.value if isinstance(c, ControlType) else c

def customize_toolbar(items, profile_name="Test"):
    config_dir = get_data_dir() / "vlc"
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
