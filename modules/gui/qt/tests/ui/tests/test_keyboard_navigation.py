from vlc_gui_automation import VLCTests, controlbar, ControlType, Keys, disabled

class TestKeyboardNavigation(VLCTests):
    video_list = ["anamorph/anamorph.mkv"]
    music_list = ["artwork_from_album/track.mp3"]
    vlc_args = VLCTests.vlc_args[2:]

    def test_focus_visible_on_progress_slider(self):
        self._open_music()
        self.assertTimeout(lambda: self.status.get_state() == "playing")
        slider = self.utils.find_element_by_automationid("progressSlider")
        self.assertTrue(self.utils.is_element_showing(slider))
        slider.grabFocus()
        self.assertTimeout(lambda: self.utils.is_element_focused(slider))

    def test_focus_visible_on_volume_slider(self):
        self._open_video()
        self.assertTimeout(lambda: self.status.get_state() == "playing")
        slider = self.utils.find_element_by_automationid("volumeSlider")
        self.assertTrue(self.utils.is_element_showing(slider))
        slider.grabFocus()
        self.assertTimeout(lambda: self.utils.is_element_focused(slider))

    def test_focus_not_lost_after_play_pause_toggle(self):
        self._open_music()
        self.assertTimeout(lambda: self.status.get_state() == "playing")
        btn = self.utils.find_element_by_automationid("playButton")
        btn.grabFocus()
        self.assertTimeout(lambda: self.utils.is_element_focused(btn))
        self.utils.send_special_key(Keys.SPACE)
        self.assertTimeout(lambda: self.status.get_state() == "paused")
        self.assertTimeout(lambda: self.utils.is_element_focused(btn))
        self.utils.send_special_key(Keys.SPACE)
        self.assertTimeout(lambda: self.status.get_state() == "playing")
        self.assertTimeout(lambda: self.utils.is_element_focused(btn))

    def test_tab_moves_focus_through_controls(self):
        self._open_music()
        self.assertTimeout(lambda: self.status.get_state() == "playing")
        self.utils.focus_window()
        self.utils.send_special_key(Keys.TAB)
        self.assertTimeout(lambda: self.utils.get_focused_element())

    def test_shift_tab_moves_focus_through_controls(self):
        self._open_music()
        self.assertTimeout(lambda: self.status.get_state() == "playing")
        self.utils.focus_window()
        self.utils.send_keycombo([Keys.SHIFT, Keys.TAB])
        self.assertTimeout(lambda: self.utils.get_focused_element())

    def test_tab_order_consistent_in_fullscreen(self):
        self._open_video()
        self.assertTimeout(lambda: self.status.get_state() == "playing")
        self.utils.focus_window()
        btn = self.utils.find_element_by_automationid("playButton")
        btn.grabFocus()
        self.utils.send_keycombo(['f'])
        self.assertTimeout(lambda: self.status.get_fullscreen() == True)
        self.utils.focus_window()
        self.utils.send_special_key(Keys.TAB)
        self.assertTimeout(lambda: self.utils.get_focused_element())

    def test_arrow_key_moves_focus_through_controls(self):
        self._open_music()
        self.assertTimeout(lambda: self.status.get_state() == "playing")
        btn = self.utils.find_element_by_automationid("playButton")
        btn.grabFocus()
        self.assertTimeout(lambda: self.utils.is_element_focused(btn))
        self.utils.send_special_key(Keys.ARROW_RIGHT)
        self.assertTimeout(lambda: self.utils.get_focused_element() != btn)

    @controlbar([ControlType.PLAY_BUTTON, ControlType.STOP_BUTTON, ControlType.NEXT_BUTTON])
    def test_focus_not_trapped_in_control_bar(self):
        self._open_music()
        self.assertTimeout(lambda: self.status.get_state() == "playing")
        bar = self.utils.find_element_by_automationid("miniplayerControlBar")
        self.assertTimeout(lambda: self.utils.is_element_showing(bar))
        btn = self.utils.find_element_by_automationid("playButton")
        btn.grabFocus()
        self.assertTimeout(lambda: self.utils.is_element_focused(btn))
        for _ in range(len(bar.children)):
            self.utils.send_special_key(Keys.TAB)
        focused = self.utils.get_focused_element()
        self.assertNotIn(focused.accessible_id, {"playButton", "stopButton", "nextButton"})