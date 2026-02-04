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
from unittest import skip, skipUnless
from functools import wraps
from .exceptions import ElementNotFoundError

# Decorators for test functions
windows_only = skipUnless(system() == "Windows", "Windows-only test")
linux_only = skipUnless(system() == "Linux", "Linux-only test")
disabled = skip("This test is disabled.")

def fail_on_element_not_found(func):
    @wraps(func)
    def wrapper(self, *args, **kwargs):
        try:
            func(self, *args, **kwargs)
            return True
        except ElementNotFoundError as e:
            return False
    return wrapper

def controlbar(items):
    def decorator(func):
        func.controlbar = list(items)
        return func
    return decorator