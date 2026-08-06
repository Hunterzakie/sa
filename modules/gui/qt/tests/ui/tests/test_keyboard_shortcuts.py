from vlc_gui_automation import VLCTests, Keys, disabled, ControlType

video = "anamorph/anamorph.mkv"
track = "artwork_from_album/track.mp3"
track2 = "artwork_from_album/track2.mp3"

class TestCoreShortcut(VLCTests):
    video_list = [video]
    music_list = [track, track2]
    vlc_args = VLCTests.vlc_args + []
    controlbar = [ControlType.PLAY_BUTTON, ControlType.PLAYER_SWITCH_BUTTON]

    def test_s_stops_video(self):
        self._open_video()
        self.assertTimeout(lambda: self.status.get_state() == "playing")
        self.assertTimeout(lambda: self.utils.is_element_showing(
            self.utils.find_element_by_automationid("playerView")))
        self.utils.focus_window()
        btn = self.utils.find_element_by_automationid("playButton")
        btn.grabFocus()
        self.utils.send_keycombo(['s'])
        self.assertTimeout(lambda: self.status.get_state() == "stopped")

    def test_s_stops_music(self):
        self._open_music()
        self.assertTimeout(lambda: self.status.get_state() == "playing")
        self.utils.click_element_by_automationid("playerSwitchButton")
        self.assertTimeout(lambda: self.utils.is_element_showing(
            self.utils.find_element_by_automationid("playerView")))
        self.utils.focus_window()
        btn = self.utils.find_element_by_automationid("playButton")
        btn.grabFocus()
        self.utils.send_keycombo(['s'])
        self.assertTimeout(lambda: self.status.get_state() == "stopped")

    def test_s_stops_in_ml_view(self):
        self._open_music()
        self.assertTimeout(lambda: self.status.get_state() == "playing")
        self.assertTimeout(lambda: self.utils.is_element_showing(
            self.utils.find_element_by_automationid("mainDisplay")))
        self.utils.focus_window()
        btn = self.utils.find_element_by_automationid("playButton")
        btn.grabFocus()
        self.utils.send_keycombo(['s'])
        self.assertTimeout(lambda: self.status.get_state() == "stopped")

class TestCoreShortcutNotEmbedded(VLCTests):
    video_list = [video]
    music_list = [track, track2]
    vlc_args = VLCTests.vlc_args + ["--no-embedded-video"]

    def test_s_stops_video_not_embedded(self):
        self._open_video()
        self.assertTimeout(lambda: self.status.get_state() == "playing")
        self.utils.click_element_by_automationid("playButton")
        self.utils.send_keycombo(['s'])
        self.assertTimeout(lambda: self.status.get_state() == "stopped")

class TestCoreShortcutMinimal(VLCTests):
    video_list = [video]
    vlc_args = VLCTests.vlc_args + ["--qt-minimal-view"]

    def test_s_stops_in_minimal_view(self):
        self.assertTimeout(lambda: not self.utils.get_focused_element())
        self._open_video()
        self.assertTimeout(lambda: self.status.get_state() == "playing")
        self.utils.focus_window()
        self.utils.send_keycombo(['s'])
        self.assertTimeout(lambda: self.status.get_state() == "stopped")

# Broken Behavior: Globalshortcuts do not fire in the test enviroment
@disabled
class TestQtUiShortcuts(VLCTests):
    video_list = [video]
    music_list = [track, track2]
    vlc_args = VLCTests.vlc_args + []

    def test_ctrl_p_opens_preferences(self):
        before = self.utils.get_count_criteria(element_type="dialog")
        self.utils.send_keycombo([Keys.CONTROL, 'p'])
        self.assertTimeout(
            lambda: self.utils.get_count_criteria(element_type="dialog") > before
        )
        self.utils.send_special_key(Keys.ESCAPE)

    def test_ctrl_shift_o_opens_file_dialog(self):
        before = self.utils.get_count_criteria(element_type="dialog")
        self.utils.send_keycombo([Keys.CONTROL, Keys.SHIFT, 'o'])
        self.assertTimeout(
            lambda: self.utils.get_count_criteria(element_type="dialog") > before
        )
        self.utils.send_special_key(Keys.ESCAPE)

    def test_ctrl_d_opens_disc_dialog(self):
        before = self.utils.get_count_criteria(element_type="dialog")
        self.utils.send_keycombo([Keys.CONTROL, 'd'])
        self.assertTimeout(
            lambda: self.utils.get_count_criteria(element_type="dialog") > before
        )
        self.utils.send_special_key(Keys.ESCAPE)

    def test_ctrl_n_opens_network_dialog(self):
        before = self.utils.get_count_criteria(element_type="dialog")
        self.utils.send_keycombo([Keys.CONTROL, 'n'])
        self.assertTimeout(
            lambda: self.utils.get_count_criteria(element_type="dialog") > before
        )
        self.utils.send_special_key(Keys.ESCAPE)

    def test_ctrl_c_opens_capture_dialog(self):
        before = self.utils.get_count_criteria(element_type="dialog")
        self.utils.send_keycombo([Keys.CONTROL, 'c'])
        self.assertTimeout(
            lambda: self.utils.get_count_criteria(element_type="dialog") > before
        )
        self.utils.send_special_key(Keys.ESCAPE)

    def test_ctrl_r_opens_convert_dialog(self):
        before = self.utils.get_count_criteria(element_type="dialog")
        self.utils.send_keycombo([Keys.CONTROL, 'r'])
        self.assertTimeout(
            lambda: self.utils.get_count_criteria(element_type="dialog") > before
        )
        self.utils.send_special_key(Keys.ESCAPE)

    def test_ctrl_s_opens_stream_output_dialog(self):
        before = self.utils.get_count_criteria(element_type="dialog")
        self.utils.send_keycombo([Keys.CONTROL, 's'])
        self.assertTimeout(
            lambda: self.utils.get_count_criteria(element_type="dialog") > before
        )
        self.utils.send_special_key(Keys.ESCAPE)

    def test_ctrl_e_opens_effects_dialog(self):
        before = self.utils.get_count_criteria(element_type="dialog")
        self.utils.send_keycombo([Keys.CONTROL, 'e'])
        self.assertTimeout(
            lambda: self.utils.get_count_criteria(element_type="dialog") > before
        )
        self.utils.send_special_key(Keys.ESCAPE)

    def test_ctrl_i_opens_media_info(self):
        before = self.utils.get_count_criteria(
            name="Current Media Information — VLC media player"
        )
        self.utils.send_keycombo([Keys.CONTROL, 'i'])
        self.assertTimeout(
            lambda: self.utils.get_count_criteria(
                name="Current Media Information — VLC media player"
            ) > before
        )
        self.utils.send_special_key(Keys.ESCAPE)

    def test_ctrl_j_opens_codec_info(self):
        before = self.utils.get_count_criteria(
            name="Current Media Information — VLC media player"
        )
        self.utils.send_keycombo([Keys.CONTROL, 'j'])
        self.assertTimeout(
            lambda: self.utils.get_count_criteria(
                name="Current Media Information — VLC media player"
            ) > before
        )
        self.utils.send_special_key(Keys.ESCAPE)

    def test_ctrl_m_opens_messages_dialog(self):
        before = self.utils.get_count_criteria(name="Messages — VLC media player")
        self.utils.send_keycombo([Keys.CONTROL, 'm'])
        self.assertTimeout(
            lambda: self.utils.get_count_criteria(
                name="Messages — VLC media player"
            ) > before
        )
        self.utils.send_special_key(Keys.ESCAPE)

    def test_ctrl_t_opens_goto_time_dialog(self):
        before = self.utils.get_count_criteria(element_type="dialog")
        self.utils.send_keycombo([Keys.CONTROL, 't'])
        self.assertTimeout(
            lambda: self.utils.get_count_criteria(element_type="dialog") > before
        )
        self.utils.send_special_key(Keys.ESCAPE)

    def test_ctrl_shift_w_opens_vlm_dialog(self):
        before = self.utils.get_count_criteria(name="VLM configurator — VLC media player")
        self.utils.send_keycombo([Keys.CONTROL, Keys.SHIFT, 'w'])
        self.assertTimeout(
            lambda: self.utils.get_count_criteria(
                name="VLM configurator — VLC media player"
            ) > before
        )
        self.utils.send_special_key(Keys.ESCAPE)

    def test_f1_opens_help_dialog(self):
        before = self.utils.get_count_criteria(name="Help — VLC media player")
        self.utils.send_special_key(Keys.F1)
        self.assertTimeout(
            lambda: self.utils.get_count_criteria(
                name="Help — VLC media player"
            ) > before
        )
        self.utils.send_special_key(Keys.ESCAPE)

    def test_ctrl_h_opens_minimal_view(self):
        display = self.utils.find_element_by_automationid("mainDisplay")
        self.assertTimeout(lambda: self.utils.is_element_showing(display))
        self.utils.focus_window()
        self.utils.send_keycombo([Keys.CONTROL, 'h'])
        self.assertTimeout(lambda: not self.utils.is_element_showing(display))

    def test_ctrl_l_opens_playqueue_panel(self):
        toggle = self.utils.find_element_by_name("Playlist", "push button")
        before = toggle.checked
        self.utils.send_keycombo([Keys.CONTROL, 'l'])
        self.assertTimeout(lambda: toggle.checked != before)

    def test_ctrl_b_opens_bookmarks_dialog(self):
        before = self.utils.get_count_criteria(name="Edit Bookmarks — VLC media player")
        self.utils.send_keycombo([Keys.CONTROL, 'b'])
        self.assertTimeout(
            lambda: self.utils.get_count_criteria(
                name="Edit Bookmarks — VLC media player"
            ) > before
        )
        self.utils.send_special_key(Keys.ESCAPE)

    def test_ctrl_q_quits(self):
        self.utils.focus_window()
        self.utils.send_keycombo([Keys.CONTROL, 'q'])
        self.assertTimeout(lambda: self.vlc_process.poll() is not None, timeout=10)