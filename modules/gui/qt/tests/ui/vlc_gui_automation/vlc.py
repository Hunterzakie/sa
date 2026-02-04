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

import logging
from time import sleep
from .enums import Keys
from .utils import BaseDriverUtils
from .exceptions import ElementNotFoundError
from .decorators import fail_on_element_not_found

class VLCState:
    def __init__(self, utils: BaseDriverUtils):
        self.utils = utils

    def is_on_main_window(self):
        number = self.utils.get_count_criteria(element_type="dialog")
        logging.debug(f"Number of dialogs found: {number}")
        if number == 0:
            return True
        return False

    @fail_on_element_not_found
    def is_on_home_tab(self):
        self.utils.find_element_by_name("Home")
    
    @fail_on_element_not_found
    def is_on_ml_category(self):
        self.utils.find_element_by_automationid("ml-groupbox")
        
    @fail_on_element_not_found
    def is_on_preferences_window(self):
        self.utils.find_element_by_name("Simple Preferences")
    
    @fail_on_element_not_found
    def is_media_opened(self):
        self.utils.find_element_by_name("Player controls", element_type="panel")


class VLCActions:
    def __init__(self, utils: BaseDriverUtils, test_case):
        self.utils = utils
        self.state = VLCState(utils)
        self.test_case = test_case
    
    # Navigation shortcuts
    def navigate_to_home_tab(self):
        """Navigate to home tab"""
        self.test_case.assertTrue(self.state.is_on_main_window())
        self.utils.click_element_by_automationid("homeNavBar")
        self.test_case.assertTrue(self.state.is_on_home_tab())

    def navigate_to_video_tab(self):
        """Navigate to video tab"""
        self.test_case.assertTrue(self.state.is_on_main_window())
        self.utils.click_element_by_automationid("videoNavBar")

    def navigate_to_music_tab(self):
        """Navigate to music tab"""
        self.test_case.assertTrue(self.state.is_on_main_window())
        self.utils.click_element_by_automationid("musicNavBar")

    def navigate_to_browse_tab(self):
        """Navigate to browse tab"""
        self.test_case.assertTrue(self.state.is_on_main_window())
        self.utils.click_element_by_automationid("browseNavBar")
    
    def navigate_to_discover_tab(self):
        """Navigate to discover tab"""
        self.test_case.assertTrue(self.state.is_on_main_window())
        self.utils.click_element_by_automationid("discoverNavBar")

    def navigate_to_media_library_category(self):
        """Navigate to preferences > media library"""
        self.test_case.assertTrue(self.state.is_on_main_window())
        self.open_preferences_window()
        self.utils.click_element_by_automationid("mlCategory")
        self.test_case.assertTrue(self.state.is_on_ml_category())

    # Opens window
    def open_preferences_window(self):
        """Open preferences using keyboard shortcut (Ctrl+P)"""
        self.test_case.assertTrue(self.state.is_on_main_window())
        self.utils.send_keycombo([Keys.CONTROL, 'p'])
        self.test_case.assertTrue(self.state.is_on_preferences_window())
    
    # File operations
    def open_media_by_path(self, media_path: str):
        self.test_case.assertTrue(self.state.is_on_home_tab())
        self.utils.click_element_by_automationid("openFile")
        sleep(0.5)
        self.utils.send_keys(str(media_path))
        self.utils.send_special_key(Keys.ENTER)
        self.test_case.assertTimeout(
            self.state.is_media_opened, timeout=10
        )

    # Verifying operations
    def verify_is_item_in_favorites(self, item_name: str) -> bool:
        favorites_tab = self.utils.find_element_by_name("Favorites")
        parent_of_parent_favorites = self.utils.get_element_parent(favorites_tab, 2)
        
        try:
            self.utils.find_element_by_name(item_name, source=parent_of_parent_favorites)
            return True
        except ElementNotFoundError:
            return False