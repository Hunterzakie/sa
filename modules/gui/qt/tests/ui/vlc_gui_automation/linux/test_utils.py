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

from datetime import datetime
from dogtail import rawinput, utils
from dogtail.tree import SearchError
from dogtail.predicate import GenericPredicate
from mss import mss
from time import sleep
from pathlib import Path
from ..utils import BaseDriverUtils
from ..enums import Keys
from ..exceptions import ElementNotFoundError
from gi.repository import Atspi

DOGTAIL_KEYS = {
    Keys.TAB: 'Tab',
    Keys.RETURN: 'Return',
    Keys.ENTER: 'Enter',
    Keys.ESCAPE: 'Escape',
    Keys.SPACE: 'space',
    Keys.BACKSPACE: 'BackSpace',
    Keys.DELETE: 'Delete',
    Keys.CONTROL: 'Control_L',
    Keys.SHIFT: 'Shift_L',
    Keys.ALT: 'Alt_L',
    Keys.ARROW_UP: 'Up',
    Keys.ARROW_DOWN: 'Down',
    Keys.ARROW_LEFT: 'Left',
    Keys.ARROW_RIGHT: 'Right',
    Keys.MENU: 'Menu',
    Keys.F1: 'F1',
    Keys.F2: 'F2',
    Keys.F3: 'F3',
    Keys.F4: 'F4',
    Keys.F5: 'F5',
    Keys.F6: 'F6',
    Keys.F7: 'F7',
    Keys.F8: 'F8',
    Keys.F9: 'F9',
    Keys.F10: 'F10',
    Keys.F11: 'F11',
    Keys.F12: 'F12',
    Keys.PLUS: 'KP_Add',
    Keys.MINUS: 'minus',
    Keys.EQUAL: 'equal',
}

class LinuxDriverUtils(BaseDriverUtils):
    def get_children_count(self, element):
        return len(element.children)
    
    def wait_until(self, condition, timeout=5, interval=0.2):
        for _ in range(int(timeout / interval)):
            if condition():
                return True
            sleep(interval)
        return False
    
    def get_count_criteria(self, name=None, element_type=None, source=None) -> int:
        source = source or self.driver
        predicate = GenericPredicate(name=name, role_name=element_type)
        return len(source.find_children(predicate))
    
    def is_element_showing(self, element) -> bool:
        state_set = element.get_state_set()
        return (
            state_set.contains(Atspi.StateType.ENABLED) and
            state_set.contains(Atspi.StateType.SENSITIVE)
        )
    
    def is_element_focused(self, element) -> bool:
        return Atspi.StateType.FOCUSED in element.state_set

    def get_focused_element(self, source=None):
        source = source or self.driver
        for child in source.children:
            if Atspi.StateType.FOCUSED in child.state_set:
                return child
            result = self.get_focused_element(source=child)
            if result is not None:
                return result
        return None
    
    def get_element_value(self, element):
        try:
            return element.queryValue().currentValue
        except Exception:
            return None

    def get_element_parent(self, element, iterations=1):
        new_element = element
        for _ in range(iterations):
            new_element = new_element.parent
            if new_element is None:
                raise Exception("There is no parent of this element")
        return new_element

    def find_element_by_name(self, name, element_type=None, source=None):
        source = source or self.driver
        try:
            if element_type is None:
                element = source.child(name=name)
            else:
                element = source.child(name=name, role_name=element_type)
        except SearchError:
            type_msg = f" and type '{element_type}'" if element_type else ""
            raise ElementNotFoundError(f"Element with name '{name}'{type_msg} not found.")
        return element

    # Name on Windows and Linux, tied to Qt's Accessible.name on Windows and Linux.
    def click_element_by_name(self, name, element_type=None, source=None):
        source = source or self.driver
        element = self.find_element_by_name(name, element_type, source)
        element.click()
    
    # Tied to Qt's Accessible.description on Windows and Linux.
    def find_element_by_description(self, description, source=None):
        source = source or self.driver
        try:
            element = source.child(description=description)
        except SearchError:
            raise ElementNotFoundError(f"Element with description '{description}' not found.")
        return element

    def click_element_by_description(self, description, source=None):
        source = source or self.driver
        element = self.find_element_by_description(description, source)
        element.click()
    
    def find_element_by_automationid(self, automationid, source=None):
        source = source or self.driver
        try:
            element = source.child(identifier=automationid)
        except SearchError:
            raise ElementNotFoundError(f"Element with AutomationID '{automationid}' not found.")
        return element
    
    def find_elements_by_automationid(self, automationid, source=None):
        source = source or self.driver
        matches = []
        for child in source.children:
            if getattr(child, 'identifier', None) == automationid:
                matches.append(child)
            matches.extend(self.find_elements_by_automationid(automationid, source=child))
        return matches

    # AutomationID on Windows, which is tied to Accessible.id in Qt Windows. Maybe be tied to objectName in Qt Linux 
    def click_element_by_automationid(self, automationid, source=None):
        source = source or self.driver
        element = self.find_element_by_automationid(automationid, source)
        element.do_action(0)

    def hover_element(self, element):
        x, y = element.position
        width, height = element.size
        center_x = x + width // 2
        center_y = y + height // 2
        rawinput.absolute_motion(center_x, center_y)

    def focus_element(self, element):
        x, y = element.position
        width, height = element.size
        rawinput.click(x + width // 2, y + height // 2)

    def long_click_element(self, element, duration):
        x, y = element.position
        width, height = element.size
        center_x = x + width // 2
        center_y = y + height // 2
        rawinput.press(center_x, center_y)
        sleep(duration)
        rawinput.release(center_x, center_y)

    def focus_window(self):
        for child in self.driver.children:
            if child.role == "frame" or child.role == "dialog":
                child.grabFocus()
                rawinput.click(child.position[0] + 1, child.position[1] + 1)
                sleep(0.3)
                return
        self.driver.grabFocus()

    def long_keypress(self, key: Keys | str, duration: float | int):
        match key:
            case Keys():
                dogtail_key = DOGTAIL_KEYS.get(key)
                if dogtail_key is None:
                    raise Exception(f"Key '{key}' not mapped in DOGTAIL_KEYS.")
            case str():
                dogtail_key = key
        rawinput.hold_key(dogtail_key)
        sleep(duration)
        rawinput.release_key(dogtail_key)

    def send_keys(self, keys: str):
        rawinput.type_text(keys)

    def send_special_key(self, key: Keys):
        rawinput.press_key(DOGTAIL_KEYS.get(key))

    def send_keycombo(self, keycombo: list[Keys | str]) -> None:
        keys_str = ""
        for key in keycombo:
            match key:
                case Keys():
                    mapped_key = DOGTAIL_KEYS.get(key)
                    if mapped_key is None:
                        raise Exception(f"Key '{key}' not mapped in DOGTAIL_KEYS.")
                    keys_str += f"<{mapped_key}>"
                case str():
                    keys_str += key 
        rawinput.key_combo(keys_str)

    def take_screenshot(self, path=Path("screenshots")):
        path.mkdir(parents=True, exist_ok=True)
        timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
        file_path = path / f"screenshot_{timestamp}.png"
        with mss() as sct:
            sct.shot(output=str(file_path))