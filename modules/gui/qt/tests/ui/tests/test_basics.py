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


from vlc_gui_automation import VLCTests, disabled, Keys

class TestBasics(VLCTests):
    video_list = ["anamorph/anamorph.mkv"]
    music_list = ["artwork_from_album/track.mp3"]
    vlc_args = VLCTests.vlc_args + [] # Extra VLC args if needed

    def _expand_sidebar(self):
        self.utils.click_element_by_automationid("sidebarToggleButton")
        self.assertTimeout(lambda: self.utils.is_element_showing(
            self.utils.find_element_by_automationid("homeNavBar"))
        )
        return self.utils.find_element_by_name("Sidebar")

    def test_navigate_to_all_tabs(self):
        sidebar = self._expand_sidebar()

        self.vlc.navigate_to_discover_tab()
        discover = self.utils.find_element_by_automationid("discoverNavBar")
        self.assertTimeout(lambda: self.utils.is_element_showing(discover))

        self.utils.find_element_by_name("URL", source=sidebar).do_action(0)
        self.assertTimeout(lambda: self.utils.is_element_showing(
            self.utils.find_element_by_name("URL", element_type="label")))

        self.vlc.navigate_to_browse_tab()
        browse = self.utils.find_element_by_automationid("browseNavBar")
        self.assertTimeout(lambda: self.utils.is_element_showing(browse))

        self.vlc.navigate_to_music_tab()
        music = self.utils.find_element_by_automationid("musicNavBar")
        self.assertTimeout(lambda: self.utils.is_element_showing(music))

        for subtab in ["Artists", "Albums", "Tracks", "Genres", "Playlists"]:
            self.utils.find_element_by_name(subtab, source=sidebar).do_action(0)
            self.assertTimeout(lambda: self.utils.is_element_showing(
                self.utils.find_element_by_name(subtab, element_type="label")
                )
            )

        self.vlc.navigate_to_video_tab()
        video = self.utils.find_element_by_automationid("videoNavBar")
        self.assertTimeout(lambda: self.utils.is_element_showing(video))

        self.utils.find_element_by_name("Playlists", source=sidebar).do_action(0)
        self.assertTimeout(lambda: self.utils.is_element_showing(
            self.utils.find_element_by_name("Playlists", element_type="label")))

        self.vlc.navigate_to_home_tab()
        home = self.utils.find_element_by_automationid("homeNavBar")
        self.assertTimeout(lambda: self.utils.is_element_showing(home))


    def test_history_back_button_navigates_back(self):
        self._expand_sidebar()
        self.vlc.navigate_to_discover_tab()
        discover = self.utils.find_element_by_automationid("discoverNavBar")
        self.assertTimeout(lambda: self.utils.is_element_showing(discover))
        self.utils.click_element_by_automationid("historyBackButton")
        self.assertTimeout(lambda: self.utils.is_element_showing(
            self.utils.find_element_by_automationid("homeNavBar"))
        )

    def test_main_window_visible(self):
        window = self.utils.driver.child(role_name="frame")
        self.assertIsNotNone(window)
        self.assertTimeout(lambda: self.utils.is_element_showing(window))

    def test_app_launches_without_crash(self):
        self.assertIsNotNone(self.vlc_process)
        self.assertEqual(self.vlc_process.poll(), None)
        self.assertTimeout(lambda: self.utils.get_focused_element())