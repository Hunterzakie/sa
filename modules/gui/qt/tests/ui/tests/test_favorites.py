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

from vlc_gui_automation import VLCTests, disabled

class TestFavorites(VLCTests):
    video_list = ["anamorph/anamorph.mkv"]
    music_list = ["artwork_from_album/track.mp3"]
    vlc_args = VLCTests.vlc_args + [] # Extra VLC args if needed

    # Blocked by QTBUG-110624: Qt context menus are not exposed to AT-SPI.
    @disabled
    def test_favorite_music_and_verify_if_its_applied(self):
        self.utils.click_element_by_automationid("musicNavBar")
        row = self.utils.find_element_by_name("Track 1")
        self.assertTrue(self.utils.is_element_showing(row))
        self.utils.hover_element(row)
        self.utils.click_element_by_name("Add to favorites")

        self.utils.click_element_by_automationid("homeNavBar")
        self.assertTrue(self.vlc.verify_is_item_in_favorites("Track 1"),
                        "Track 1 was not found in favorites.")