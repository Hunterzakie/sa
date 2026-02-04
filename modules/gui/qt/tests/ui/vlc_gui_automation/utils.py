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

from pathlib import Path
from abc import ABC, abstractmethod
from .enums import Keys

class BaseDriverUtils(ABC):
    def __init__(self, driver):
        self.driver = driver

    # Mouse utilities

    # Name on Windows and Linux, tied to Qt's Accessible.name on Windows and Linux.
    @abstractmethod
    def click_element_by_name(self, name, element_type=None, source=None):
        pass

    @abstractmethod
    def click_element_by_description(self, description, source=None):
        pass

    # AutomationID on Windows UIA, ID on Linux AT-SPI. Tied to Accessible.id in Qt
    @abstractmethod
    def click_element_by_automationid(self, automationid, source=None):
        pass

    @abstractmethod
    def hover_element(self, element):
        pass

    @abstractmethod
    def focus_element(self, element):
        pass

    @abstractmethod
    def long_click_element(self, element, duration):
        pass

    @abstractmethod
    def focus_window(self):
        pass

    # Test utilities

    def get_root_element(self):
        return self.driver

    @abstractmethod
    def is_element_showing(self, element) -> bool:
        pass

    @abstractmethod
    def is_element_focused(self, element) -> bool:
        pass

    @abstractmethod
    def get_focused_element(self, source=None):
        pass

    @abstractmethod
    def get_children_count(self, element) -> int:
        pass

    @abstractmethod
    def get_count_criteria(self, name=None, element_type=None, source=None) -> int:
        """ Returns the count of elements matching the criteria """
        pass
    
    @abstractmethod
    def get_element_parent(self, element, iterations=1):
        pass
    
    @abstractmethod
    def find_element_by_name(self, name, element_type=None, source=None):
        pass

    @abstractmethod
    def find_element_by_description(self, description, source=None):
        pass

    @abstractmethod
    def find_element_by_automationid(self, automationid, source=None):
        pass

    @abstractmethod
    def find_elements_by_automationid(self, automationid, source=None):
        pass

    # Keyboard actions

    @abstractmethod
    def long_keypress(self, key: Keys | str, duration: float | int):
        pass

    @abstractmethod
    def send_keys(self, keys):
        pass

    @abstractmethod
    def send_special_key(self, key: Keys):
        pass

    @abstractmethod
    def send_keycombo(self, keycombo):
        """
        Has to be sent in list format of Keys or characters.
        Example: [Keys.CONTROL, 'c'] to send "Ctrl+C"
        You can see the full list of supported keys in the Keys enum variable in vlc_gui_automation/enums.py
        """
        pass

    # OS Utilities

    @abstractmethod
    def take_screenshot(self, path=Path("screenshots")):
        pass
