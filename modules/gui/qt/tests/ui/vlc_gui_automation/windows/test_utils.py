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
from appium.webdriver.common.appiumby import AppiumBy
from selenium.webdriver.common.action_chains import ActionChains
from selenium.webdriver.common.keys import Keys as SeleniumKeys
from selenium.common.exceptions import NoSuchElementException
from base64 import b64decode
from time import sleep
from pathlib import Path
from ..utils import BaseDriverUtils
from ..enums import Keys
from ..exceptions import ElementNotFoundError

SELENIUM_KEYS = {
    Keys.TAB: SeleniumKeys.TAB,
    Keys.RETURN: SeleniumKeys.RETURN,
    Keys.ENTER: SeleniumKeys.ENTER,
    Keys.ESCAPE: SeleniumKeys.ESCAPE,
    Keys.SPACE: SeleniumKeys.SPACE,
    Keys.BACKSPACE: SeleniumKeys.BACKSPACE,
    Keys.DELETE: SeleniumKeys.DELETE,
    Keys.CONTROL: SeleniumKeys.CONTROL,
    Keys.SHIFT: SeleniumKeys.SHIFT,
    Keys.ALT: SeleniumKeys.ALT,
    Keys.ARROW_UP: SeleniumKeys.ARROW_UP,
    Keys.ARROW_DOWN: SeleniumKeys.ARROW_DOWN,
    Keys.ARROW_LEFT: SeleniumKeys.ARROW_LEFT,
    Keys.ARROW_RIGHT: SeleniumKeys.ARROW_RIGHT,
    Keys.MENU: None,  # No direct equivalent in SeleniumKeys
    Keys.F1: SeleniumKeys.F1,
    Keys.F2: SeleniumKeys.F2,
    Keys.F3: SeleniumKeys.F3,
    Keys.F4: SeleniumKeys.F4,
    Keys.F5: SeleniumKeys.F5,
    Keys.F6: SeleniumKeys.F6,
    Keys.F7: SeleniumKeys.F7,
    Keys.F8: SeleniumKeys.F8,
    Keys.F9: SeleniumKeys.F9,
    Keys.F10: SeleniumKeys.F10,
    Keys.F11: SeleniumKeys.F11,
    Keys.F12: SeleniumKeys.F12,
    Keys.PLUS: "+",
    Keys.MINUS: "-",
    Keys.EQUAL: "=",
}

DOGTAIL_TO_SELENIUM = {
    'push button': 'Button',
    'page tab': 'TabItem',
    'panel': 'Pane',
    'list item': 'ListItem',
    'check box': 'CheckBox',
    'radio button': 'RadioButton',
    'combo box': 'ComboBox',
    'menu item': 'MenuItem',
    'tab': 'Tab',
    'tab list': 'TabList',
    'text': 'Text',
    'window': 'Window',
    'dialog': 'Window',
}

class WindowsDriverUtils(BaseDriverUtils):
    def get_children_count(self, element):
        try:
            children = element.find_elements(by=AppiumBy.XPATH, value="./*")
            return len(children)
        except NoSuchElementException:
            return 0
        
    def wait_until(self, condition, timeout=5, interval=0.2):
        for _ in range(int(timeout / interval)):
            if condition():
                return True
            sleep(interval)
        return False
        
    def get_count_criteria(self, name=None, element_type=None, source=None) -> int:
        source = source or self.driver
        xpath_parts = []
        if name is not None:
            xpath_parts.append(f"@Name='{name}'")
        if element_type is not None:
            xpath_parts.append(f"@ControlType='{DOGTAIL_TO_SELENIUM.get(element_type)}'")
        if len(xpath_parts) > 0:
            xpath_string = ".//*[" + " and ".join(xpath_parts) + "]"
        else:
            xpath_string = ".//*"
        elements = source.find_elements(by=AppiumBy.XPATH, value=xpath_string)
        return len(elements)

    def get_element_parent(self, element, iterations=1):
        xpath_string = ".."
        for _ in range(iterations - 1):
            xpath_string += "/.."
        new_element = element.find_element(by=AppiumBy.XPATH, value=xpath_string)
        return new_element

    def find_element_by_name(self, name, element_type=None, source=None):
        source = source or self.driver
        try:
            if element_type is None:
                element = source.find_element(by=AppiumBy.NAME, value=name)
            else:
                element = source.find_element(by=AppiumBy.XPATH, value=f".//*[@ControlType='{DOGTAIL_TO_SELENIUM.get(element_type)}' and @Name='{name}']")
        except NoSuchElementException:
            type_msg = f" and type '{element_type}'" if element_type else ""
            raise ElementNotFoundError(f"Element with name '{name}'{type_msg} not found.")
        return element

    # Tied to Qt's Accessible.name
    def click_element_by_name(self, name, element_type=None, source=None):
        source = source or self.driver
        element = self.find_element_by_name(name, element_type, source)
        element.click()
        
    def find_element_by_description(self, description, source=None):
        source = source or self.driver
        try:
            element = source.find_element(by=AppiumBy.XPATH, value=f".//*[@FullDescription='{description}']")
        except NoSuchElementException:
            raise ElementNotFoundError(f"Element with description '{description}' not found.")
        return element
    
    def click_element_by_description(self, description, source=None):
        source = source or self.driver
        element = self.find_element_by_description(description, source)
        element.click()

    def find_element_by_automationid(self, automationid, source=None):
        source = source or self.driver
        try:
            element = source.find_element(by=AppiumBy.ACCESSIBILITY_ID, value=automationid)
        except NoSuchElementException:
            raise ElementNotFoundError(f"Element with automation id '{automationid}' not found.")
        return element
    
    def find_elements_by_automationid(self, automationid, source=None):
        source = source or self.driver
        return source.find_elements(by=AppiumBy.ACCESSIBILITY_ID, value=automationid)

    # AutomationID on Windows, tied to Accessible.id in Qt.
    def click_element_by_automationid(self, automationid, source=None):
        source = source or self.driver
        element = self.find_element_by_automationid(automationid, source)
        element.click()

    def hover_element(self, element):
        actions = ActionChains(self.driver)
        actions.move_to_element(element).perform()

    def focus_element(self, element):
        actions = ActionChains(self.driver)
        actions.click(element).perform()

    def long_click_element(self, element, duration: float | int):
        actions = ActionChains(self.driver)
        actions.click_and_hold(element)
        actions.pause(duration)
        actions.release()
        actions.perform()

    def focus_window(self):
        try:
            window = self.driver.find_element(by=AppiumBy.XPATH, value=".//*[self::Window or self::Pane]")
            window.click()
        except NoSuchElementException:
            pass

    def long_keypress(self, key: Keys | str, duration: float | int):
        match key:
            case Keys():
                selenium_key = SELENIUM_KEYS.get(key)
                if selenium_key is None:
                    raise Exception(f"Key '{key}' not mapped in SELENIUM_KEYS.")
            case str():
                selenium_key = key
        actions = ActionChains(self.driver)
        actions.key_down(selenium_key)
        actions.pause(duration)
        actions.key_up(selenium_key)
        actions.perform()

    def send_keys(self, keys: str):
        actions = ActionChains(self.driver)
        actions.send_keys(keys).perform()

    def send_special_key(self, key: Keys):
        if key == Keys.MENU:
            self.driver.execute_script("windows: keys", {
                'actions': [{
                    "virtualKeyCode": 0x5D,
                    "down": True
                }, {
                    "virtualKeyCode": 0x5D,
                    "down": False
                }]
            })
            sleep(0.1)
            return
        actions = ActionChains(self.driver)
        selenium_key = SELENIUM_KEYS.get(key)
        if selenium_key is None:
            raise Exception(f"Key '{key}' does not exist in SELENIUM_KEYS")
        actions.send_keys(selenium_key).perform()
        sleep(0.1) # to avoid sending keys too fast

    def send_keycombo(self, keycombo: list[Keys | str]):
        actions = ActionChains(self.driver)

        clicked_keys = []

        for key in keycombo:
            match key:
                case Keys():
                    selenium_key = SELENIUM_KEYS.get(key)
                    if selenium_key is None:
                        raise Exception(f"Key '{key}' not mapped in SELENIUM_KEYS.")
                case str():
                    selenium_key = key
            actions.key_down(selenium_key)
            clicked_keys.append(selenium_key)


        for key in reversed(clicked_keys):
            actions.key_up(key)

        actions.perform()
        sleep(0.05) # to avoid sending keys too fast

    def take_screenshot(self, path=Path("screenshots")):
        screenshotBase64 = self.driver.get_screenshot_as_base64()
        path.mkdir(parents=True, exist_ok=True)
        timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
        filepath = path / f"screenshot_{timestamp}.png"
        with open(filepath, "wb") as f:
            f.write(b64decode(screenshotBase64))
    
    def is_element_showing(self, element) -> bool:
        return element.is_displayed()

    def is_element_focused(self, element) -> bool:
        try:
            return element.get_attribute("HasKeyboardFocus") == "True"
        except Exception:
            return False

    def get_focused_element(self, source=None):
        source = source or self.driver
        try:
            focused = source.find_element(
                by=AppiumBy.XPATH, value=".//*[@HasKeyboardFocus='True']"
            )
            return focused
        except NoSuchElementException:
            return None

    def get_element_value(self, element):
        try:
            return element.get_attribute("Value.Value")
        except Exception:
            return None