/*
 * SPDX-FileCopyrightText: 2025 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "app_mp3_playback_service.h"
#include "utils/file_browser.h"

#include <cstdint>
#include <hal/hal.h>
#include <memory>
#include <mooncake.h>
#include <string>

class AppMp3Player : public mooncake::AppAbility {
public:
    AppMp3Player();
    ~AppMp3Player();

    void onOpen() override;
    void onRunning() override;
    void onClose() override;

private:
    static constexpr int STATUS_BAR_HEIGHT   = 18;
    static constexpr int MIN_VOLUME_PERCENT  = 10;
    static constexpr int VOLUME_STEP_PERCENT = 10;

    std::unique_ptr<SdFileBrowser> _browser;
    int _key_event_slot_id = -1;
    bool _stop_requested   = false;
    std::string _status_message;

    void refresh_browser();
    void update_viewport_metrics();
    void render();
    void render_status_bar();
    void render_browser();
    void handle_key_event(const Keyboard::KeyEvent_t& keyEvent);
    void handle_player_event(audio_player_callback_event_t event);
    void adjust_volume(int percentDelta);
    void play_selected_file();
    void stop_current_song();
    void restart_current_song();
    void sync_browser_to_active_track();
    int get_volume_percent() const;
    AppMp3PlaybackService& playback();
    const AppMp3PlaybackService& playback() const;
    std::string make_display_name(const std::string& path) const;
    std::string truncate_text(const std::string& value, std::size_t maxLength) const;
};