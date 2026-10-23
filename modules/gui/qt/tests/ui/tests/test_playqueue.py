from vlc_gui_automation import VLCTests, Keys

class TestPlayqueue(VLCTests):
    video_list = ["anamorph/anamorph.mkv"]
    music_list = ["artwork_from_album/track.mp3", "artwork_from_album/track2.mp3"]
    vlc_args = VLCTests.vlc_args + []

    def _open_playqueue_panel(self):
        self.utils.click_element_by_automationid("playqueueToggle")
        pane = self.utils.find_element_by_automationid("playqueuePane")
        self.assertTimeout(lambda: self.utils.is_element_showing(pane))
        return pane

    def test_playqueue_toggle_opens_panel(self):
        self._open_music()
        self.assertTimeout(lambda: self.status.get_state() == "playing")
        btn = self.utils.find_element_by_automationid("playqueueToggle")
        self.assertTrue(self.utils.is_element_showing(btn))
        self.utils.click_element_by_automationid("playqueueToggle")
        pane = self.utils.find_element_by_automationid("playqueuePane")
        self.assertTimeout(lambda: self.utils.is_element_showing(pane))

    def test_playqueue_shows_current_track(self):
        self._open_music()
        self.assertTimeout(lambda: self.status.get_state() == "playing")
        pane = self._open_playqueue_panel()
        item = self.utils.find_element_by_name("Track 1", source=pane)
        self.assertTimeout(lambda: self.utils.is_element_showing(item))

    def test_playqueue_shows_enqueued_track(self):
        self._setup_two_tracks()
        pane = self._open_playqueue_panel()
        item1 = self.utils.find_element_by_name("Track 1", source=pane)
        self.assertTimeout(lambda: self.utils.is_element_showing(item1))
        item2 = self.utils.find_element_by_name("Track 2", source=pane)
        self.assertTimeout(lambda: self.utils.is_element_showing(item2))

    def test_playqueue_loop_toggle(self):
        self._open_music()
        self.assertTimeout(lambda: self.status.get_state() == "playing")
        pane = self._open_playqueue_panel()
        initial = self.status.get_loop()
        self.utils.click_element_by_automationid("playqueueLoop", source=pane)
        self.assertTimeout(lambda: self.status.get_loop() != initial)

    def test_playqueue_shuffle_toggle(self):
        self._open_music()
        self.assertTimeout(lambda: self.status.get_state() == "playing")
        pane = self._open_playqueue_panel()
        initial = self.status.get_random()
        self.utils.click_element_by_automationid("playqueueShuffle", source=pane)
        self.assertTimeout(lambda: self.status.get_random() != initial)

    def test_playqueue_clear(self):
        self._setup_two_tracks()
        pane = self._open_playqueue_panel()
        self.assertTimeout(lambda: self.utils.is_element_showing(
            self.utils.find_element_by_name("Track 1", source=pane)))
        self.assertTimeout(lambda: self.utils.is_element_showing(
            self.utils.find_element_by_name("Track 2", source=pane)))
        self.utils.click_element_by_automationid("playqueueClearPlayqueue", source=pane)
        self.assertTimeout(lambda: self.utils.get_count_criteria(
            name="Track 1", source=pane) == 0)
        self.assertTimeout(lambda: self.utils.get_count_criteria(
            name="Track 2", source=pane) == 0)