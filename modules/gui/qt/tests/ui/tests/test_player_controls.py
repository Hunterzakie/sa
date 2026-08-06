from vlc_gui_automation import VLCTests, controlbar, ControlType

class TestPlayerControls(VLCTests):
    video_list = ["anamorph/anamorph.mkv"]
    music_list = ["artwork_from_album/track.mp3", "artwork_from_album/track2.mp3"]
    vlc_args = VLCTests.vlc_args + []

    # Playback controls

    @controlbar([ControlType.PLAY_BUTTON])
    def test_play_button_toggles_play_pause(self):
        self._open_music()
        self.assertTimeout(lambda: self.status.get_state() == "playing")
        btn = self.utils.find_element_by_automationid("playButton")
        self.assertTimeout(lambda: self.utils.is_element_showing(btn))
        self.utils.click_element_by_automationid("playButton")
        self.assertTimeout(lambda: self.status.get_state() == "paused")
        self.utils.click_element_by_automationid("playButton")
        self.assertTimeout(lambda: self.status.get_state() == "playing")

    @controlbar([ControlType.STOP_BUTTON])
    def test_stop_button_stops_playback(self):
        self._open_music()
        self.assertTimeout(lambda: self.status.get_state() == "playing")
        btn = self.utils.find_element_by_automationid("stopButton")
        self.assertTimeout(lambda: self.utils.is_element_showing(btn))
        self.utils.click_element_by_automationid("stopButton")
        self.assertTimeout(lambda: self.status.get_state() == "stopped")

    @controlbar([ControlType.NEXT_BUTTON])
    def test_next_button_skips_to_next_track(self):
        self._setup_two_tracks()
        self.assertTimeout(lambda: self.status.get_state() == "playing")
        btn = self.utils.find_element_by_automationid("nextButton")
        self.assertTimeout(lambda: self.utils.is_element_showing(btn))
        self.utils.click_element_by_automationid("nextButton")
        self.assertTimeout(lambda: self.status.get_current_filename() == "track2.mp3")

    @controlbar([ControlType.NEXT_BUTTON, ControlType.PREVIOUS_BUTTON])
    def test_previous_button_returns_to_previous_track(self):
        self._setup_two_tracks()
        self.assertTimeout(lambda: self.status.get_state() == "playing")
        self.utils.click_element_by_automationid("nextButton")
        self.assertTimeout(lambda: self.status.get_current_filename() == "track2.mp3")
        btn = self.utils.find_element_by_automationid("previousButton")
        self.assertTimeout(lambda: self.utils.is_element_showing(btn))
        self.utils.click_element_by_automationid("previousButton")
        self.assertTimeout(lambda: self.status.get_current_filename() == "track.mp3")

    # Seeking

    @controlbar([ControlType.PLAY_BUTTON, ControlType.SKIP_BACK_BUTTON, ControlType.SKIP_FW_BUTTON])
    def test_skip_forward_button_seeks_forward(self):
        self._open_video()
        self.assertTimeout(lambda: self.status.get_state() == "playing")
        self.utils.click_element_by_automationid("playButton")
        before = self.status.get_time()
        self.utils.click_element_by_automationid("skipForwardButton")
        self.assertTimeout(lambda: self.status.get_time() >= before)

    @controlbar([ControlType.PLAY_BUTTON, ControlType.SKIP_BACK_BUTTON, ControlType.SKIP_FW_BUTTON])
    def test_skip_back_button_seeks_backward(self):
        self._open_video()
        self.assertTimeout(lambda: self.status.get_state() == "playing")
        self.utils.click_element_by_automationid("playButton")
        before = self.status.get_time()
        self.utils.click_element_by_automationid("skipForwardButton")
        self.assertTimeout(lambda: self.status.get_time() >= before)
        self.utils.click_element_by_automationid("skipBackButton")
        self.assertTimeout(lambda: self.status.get_time() <= before)

    # Playback modes

    @controlbar([ControlType.FULLSCREEN_BUTTON])
    def test_fullscreen_button_toggle(self):
        self._open_video()
        self.assertTimeout(lambda: self.status.get_state() == "playing")
        self.utils.click_element_by_automationid("fullscreenButton")
        self.assertTimeout(lambda: self.status.get_fullscreen() == True)
        self.utils.click_element_by_automationid("fullscreenButton")
        self.assertTimeout(lambda: self.status.get_fullscreen() == False)

    @controlbar([ControlType.LOOP_BUTTON])
    def test_loop_toggle(self):
        self._open_music()
        self.assertTimeout(lambda: self.status.get_state() == "playing")
        btn = self.utils.find_element_by_automationid("loopButton")
        self.assertTimeout(lambda: self.utils.is_element_showing(btn))
        self.utils.click_element_by_automationid("loopButton")
        self.assertTimeout(lambda: self.status.get_loop() == True)
        self.utils.click_element_by_automationid("loopButton")
        self.utils.click_element_by_automationid("loopButton")
        self.assertTimeout(lambda: self.status.get_loop() == False)

    @controlbar([ControlType.RANDOM_BUTTON])
    def test_shuffle_toggle(self):
        self._open_music()
        self.assertTimeout(lambda: self.status.get_state() == "playing")
        btn = self.utils.find_element_by_automationid("randomButton")
        self.assertTimeout(lambda: self.utils.is_element_showing(btn))
        self.utils.click_element_by_automationid("randomButton")
        self.assertTimeout(lambda: self.status.get_random() == True)
        self.utils.click_element_by_automationid("randomButton")
        self.assertTimeout(lambda: self.status.get_random() == False)

    @controlbar([ControlType.ATOB_BUTTON, ControlType.PLAY_BUTTON])
    def test_atob_button_cycles_a_b_loop(self):
        self._open_video()
        self.assertTimeout(lambda: self.status.get_state() == "playing")
        btn = self.utils.find_element_by_automationid("atoBButton")
        self.assertTimeout(lambda: self.utils.is_element_showing(btn))
        self.utils.click_element_by_automationid("atoBButton")
        start = self.status.get_time()
        self.assertTimeout(lambda: self.status.get_time() >= start + 2)
        self.utils.click_element_by_automationid("atoBButton")
        self.assertTimeout(lambda: self.status.get_time() <= start)

    @controlbar([ControlType.PLAY_BUTTON, ControlType.FASTER_BUTTON, ControlType.SLOWER_BUTTON])
    def test_faster_slower_buttons_change_rate(self):
        self._open_video()
        self.assertTimeout(lambda: self.status.get_state() == "playing")
        faster = self.utils.find_element_by_automationid("fasterButton")
        slower = self.utils.find_element_by_automationid("slowerButton")
        self.assertTimeout(lambda: self.utils.is_element_showing(faster))
        self.assertTimeout(lambda: self.utils.is_element_showing(slower))
        initial = self.status.get_rate()
        self.utils.click_element_by_automationid("fasterButton")
        self.assertTimeout(lambda: self.status.get_rate() > initial)
        self.utils.click_element_by_automationid("slowerButton")
        self.assertTimeout(lambda: self.status.get_rate() == initial)

    # View switching

    @controlbar([ControlType.PLAYER_SWITCH_BUTTON])
    def test_player_switch_button_toggles_view(self):
        self._open_music()
        self.assertTimeout(lambda: self.status.get_state() == "playing")
        btn = self.utils.find_element_by_automationid("playerSwitchButton")
        self.assertTimeout(lambda: self.utils.is_element_showing(btn))
        self.utils.click_element_by_automationid("playerSwitchButton")
        self.assertTimeout(lambda: self.utils.is_element_showing(
            self.utils.find_element_by_automationid("playerView")))
        self.utils.click_element_by_automationid("playerSwitchButton")
        self.assertTimeout(lambda: self.utils.is_element_showing(
            self.utils.find_element_by_automationid("homeNavBar")))

    @controlbar([ControlType.ARTWORK_INFO])
    def test_artwork_info_widget_toggles_player_view(self):
        self._open_music()
        self.assertTimeout(lambda: self.status.get_state() == "playing")
        widget = self.utils.find_element_by_automationid("artworkInfoWidget")
        self.assertTimeout(lambda: self.utils.is_element_showing(widget))
        self.utils.click_element_by_automationid("artworkInfoWidget")
        self.assertTimeout(lambda: self.utils.is_element_showing(
            self.utils.find_element_by_automationid("playerView")))
        self.utils.click_element_by_automationid("artworkInfoWidget")
        self.assertTimeout(lambda: self.utils.is_element_showing(
            self.utils.find_element_by_automationid("homeNavBar")))

    # Panels & dialogs

    def test_topbar_playqueue_button_toggles_panel(self):
        self._open_video()
        self.assertTimeout(lambda: self.status.get_state() == "playing")
        btn = self.utils.find_element_by_automationid("topBarPlayqueueButton")
        self.assertTimeout(lambda: self.utils.is_element_showing(btn))
        self.utils.click_element_by_automationid("topBarPlayqueueButton")
        pane = self.utils.find_element_by_automationid("playqueuePane")
        self.assertTimeout(lambda: self.utils.is_element_showing(pane))

    @controlbar([ControlType.PLAYLIST_BUTTON])
    def test_playqueue_button_opens_playqueue_panel(self):
        self._open_music()
        self.assertTimeout(lambda: self.status.get_state() == "playing")
        btn = self.utils.find_element_by_automationid("playqueueButton")
        self.assertTimeout(lambda: self.utils.is_element_showing(btn))
        self.utils.click_element_by_automationid("playqueueButton")
        pane = self.utils.find_element_by_automationid("playqueuePane")
        self.assertTimeout(lambda: self.utils.is_element_showing(pane))

    @controlbar([ControlType.PLAYBACK_SPEED_BUTTON])
    def test_playback_speed_button_opens_speed_dialog(self):
        self._open_video()
        self.assertTimeout(lambda: self.status.get_state() == "playing")
        btn = self.utils.find_element_by_automationid("playbackSpeedButton")
        self.assertTimeout(lambda: self.utils.is_element_showing(btn))
        self.utils.click_element_by_automationid("playbackSpeedButton")
        slider = self.utils.find_element_by_automationid("playbackSpeedSlider")
        self.assertTimeout(lambda: self.utils.is_element_showing(slider))

    @controlbar([ControlType.EXTENDED_BUTTON])
    def test_extended_settings_button_opens_effects_dialog(self):
        self._open_music()
        self.assertTimeout(lambda: self.status.get_state() == "playing")
        btn = self.utils.find_element_by_automationid("extendedSettingsButton")
        self.assertTimeout(lambda: self.utils.is_element_showing(btn))
        before = self.utils.get_count_criteria(element_type="dialog")
        self.utils.click_element_by_automationid("extendedSettingsButton")
        self.assertTimeout(lambda: self.utils.get_count_criteria(element_type="dialog") > before)

    @controlbar([ControlType.OPEN_BUTTON])
    def test_open_media_button_opens_dialog(self):
        self._open_video()
        self.assertTimeout(lambda: self.status.get_state() == "playing")
        btn = self.utils.find_element_by_automationid("openMediaButton")
        self.assertTimeout(lambda: self.utils.is_element_showing(btn))
        before = self.utils.get_count_criteria(element_type="dialog")
        self.utils.click_element_by_automationid("openMediaButton")
        self.assertTimeout(lambda: self.utils.get_count_criteria(element_type="dialog") > before)