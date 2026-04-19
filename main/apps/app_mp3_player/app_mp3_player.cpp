/*
 * SPDX-FileCopyrightText: 2025 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "app_mp3_player.h"

#include "app_sdcard/assets/tf_big.h"
#include "app_sdcard/assets/tf_small.h"
#include <apps/utils/audio/audio.h>
#include <apps/utils/common.h>
#include <apps/utils/theme.h>
#include <assets.h>
#include <hal.h>
#include <mooncake_log.h>

#include <cstring>

using namespace mooncake;

AppMp3Player::AppMp3Player()
{
    setAppInfo().name     = "MP3";
    setAppInfo().userData = new AppIcon_t(image_data_tf_big, image_data_tf_small, false);
}

AppMp3Player::~AppMp3Player()
{
    delete static_cast<AppIcon_t*>(getAppInfo().userData);
}

void AppMp3Player::onOpen()
{
    mclog::tagInfo(getAppInfo().name, "on open");

    _browser = std::make_unique<SdFileBrowser>();
    _browser->setExtensionFilter(".mp3");
    _key_event_slot_id = GetHAL().keyboard.onKeyEvent.connect(
        [this](const Keyboard::KeyEvent_t& keyEvent) { handle_key_event(keyEvent); });
    _stop_requested = false;
    _status_message.clear();

    audio::set_keyboard_sfx_enable(false);

    GetHAL().canvas.setBaseColor(THEME_COLOR_BG);
    GetHAL().canvas.setTextSize(1);
    GetHAL().canvas.setTextScroll(false);

    update_viewport_metrics();
    sync_browser_to_active_track();
    if (!playback().begin()) {
        _status_message = "Audio init failed";
    }

    bool rendered                                    = false;
    const audio_player_callback_event_t initialEvent = playback().consumeLastEvent();
    if (initialEvent != AUDIO_PLAYER_CALLBACK_EVENT_UNKNOWN) {
        handle_player_event(initialEvent);
        rendered = true;
    } else if (playback().isPlaying() && !playback().getActivePath().empty()) {
        _status_message = "Playing " + make_display_name(playback().getActivePath());
    }

    if (!rendered) {
        render();
    }
}

void AppMp3Player::onRunning()
{
    handle_player_event(playback().consumeLastEvent());

    if (is_app_exit_requested()) {
        close();
    }
}

void AppMp3Player::onClose()
{
    mclog::tagInfo(getAppInfo().name, "on close");

    if (_key_event_slot_id >= 0) {
        GetHAL().keyboard.onKeyEvent.disconnect(_key_event_slot_id);
        _key_event_slot_id = -1;
    }
    _browser.reset();

    audio::set_keyboard_sfx_enable(true);
}

void AppMp3Player::refresh_browser()
{
    if (!_browser) {
        return;
    }

    if (!GetHAL().ensureSdCardMounted()) {
        _status_message = "SD card not mounted";
        _browser->clear();
        return;
    }

    std::string errorMessage;
    if (!_browser->refresh(errorMessage)) {
        _status_message = "Browse failed: " + errorMessage;
        return;
    }

    if (_browser->getEntryCount() == 0) {
        _status_message = "No MP3 files in " + _browser->getCurrentPath();
    } else {
        _status_message = "Enter Play  ;/. Move";
    }
    _browser->setStatusMessage(_status_message);
}

void AppMp3Player::update_viewport_metrics()
{
    if (!_browser) {
        return;
    }

    const int usableHeight = GetHAL().canvas.height() - STATUS_BAR_HEIGHT;
    const std::size_t rows = usableHeight > 0 ? static_cast<std::size_t>(usableHeight / FONT_REPL_HEIGHT) : 1;
    _browser->setViewportRows(rows);
}

void AppMp3Player::render()
{
    GetHAL().canvas.fillScreen(THEME_COLOR_BG);
    render_status_bar();
    render_browser();
    GetHAL().pushCanvas();
}

void AppMp3Player::render_status_bar()
{
    GetHAL().canvas.setFont(FONT_SMALL);
    GetHAL().canvas.setTextColor(TFT_ORANGE, THEME_COLOR_BG);
    GetHAL().canvas.drawString(
        truncate_text("MP3 " + (_browser ? _browser->getCurrentPath() : std::string(SdFileBrowser::ROOT_PATH)), 36)
            .c_str(),
        0, 0);

    GetHAL().canvas.setTextColor(TFT_CYAN, THEME_COLOR_BG);
    GetHAL().canvas.drawString(truncate_text(_status_message, 36).c_str(), 0, 9);
}

void AppMp3Player::render_browser()
{
    GetHAL().canvas.setFont(FONT_REPL);

    if (_browser == nullptr || _browser->getEntryCount() == 0) {
        GetHAL().canvas.setTextColor(TFT_WHITE, THEME_COLOR_BG);
        GetHAL().canvas.drawString("(no mp3 files)", 0, STATUS_BAR_HEIGHT);
        return;
    }

    const std::size_t firstIndex = _browser->getFirstVisibleIndex();
    const std::string activePath = playback().getActivePath();
    const std::size_t maxColumns = static_cast<std::size_t>(GetHAL().canvas.width() / FONT_REPL_WIDTH);
    for (std::size_t row = 0; row < _browser->getViewportRows(); ++row) {
        const std::size_t entryIndex = firstIndex + row;
        const auto* entry            = _browser->getEntry(entryIndex);
        if (entry == nullptr) {
            break;
        }

        const bool isSelected = entryIndex == _browser->getSelectedIndex();
        const bool isPlaying  = entry->path == activePath;
        const int y           = STATUS_BAR_HEIGHT + static_cast<int>(row * FONT_REPL_HEIGHT);
        if (isSelected) {
            GetHAL().canvas.fillRect(0, y, GetHAL().canvas.width(), FONT_REPL_HEIGHT, TFT_DARKGREEN);
            GetHAL().canvas.setTextColor(TFT_WHITE, TFT_DARKGREEN);
        } else {
            GetHAL().canvas.setTextColor(isPlaying ? TFT_YELLOW : TFT_WHITE, THEME_COLOR_BG);
        }

        std::string label = isPlaying ? "> " : "  ";
        if (entry->is_parent) {
            label += "../";
        } else {
            label += entry->name;
            if (entry->is_directory) {
                label += "/";
            }
        }
        GetHAL().canvas.drawString(truncate_text(label, maxColumns).c_str(), 0, y);
    }
}

void AppMp3Player::handle_key_event(const Keyboard::KeyEvent_t& keyEvent)
{
    if (_browser == nullptr || !keyEvent.state || keyEvent.isModifier) {
        return;
    }

    bool shouldRender = false;
    if (keyEvent.keyCode == KEY_SEMICOLON || keyEvent.keyCode == KEY_UP) {
        shouldRender = _browser->moveUp();
    } else if (keyEvent.keyCode == KEY_DOT || keyEvent.keyCode == KEY_DOWN) {
        shouldRender = _browser->moveDown();
    } else if (keyEvent.keyCode == KEY_0) {
        stop_current_song();
        shouldRender = true;
    } else if (keyEvent.keyCode == KEY_BACKSPACE) {
        restart_current_song();
        shouldRender = true;
    } else if (keyEvent.keyCode == KEY_MINUS) {
        adjust_volume(-VOLUME_STEP_PERCENT);
        shouldRender = true;
    } else if (keyEvent.keyCode == KEY_EQUAL) {
        adjust_volume(VOLUME_STEP_PERCENT);
        shouldRender = true;
    } else if (keyEvent.keyCode == KEY_ENTER) {
        play_selected_file();
        shouldRender = true;
    }

    if (shouldRender) {
        render();
    }
}

void AppMp3Player::handle_player_event(audio_player_callback_event_t event)
{
    if (event == AUDIO_PLAYER_CALLBACK_EVENT_UNKNOWN) {
        return;
    }

    if (event == AUDIO_PLAYER_CALLBACK_EVENT_IDLE) {
        playback().clearActivePath();
        if (_stop_requested) {
            _status_message = "Playback stopped";
            _stop_requested = false;
        } else {
            _status_message = "Playback finished";
        }
    } else if (event == AUDIO_PLAYER_CALLBACK_EVENT_PLAYING) {
        _stop_requested = false;
        _status_message = "Playing " + make_display_name(playback().getActivePath());
    } else if (event == AUDIO_PLAYER_CALLBACK_EVENT_COMPLETED_PLAYING_NEXT) {
        _stop_requested = false;
        _status_message = "Playing " + make_display_name(playback().getActivePath());
    } else if (event == AUDIO_PLAYER_CALLBACK_EVENT_PAUSE) {
        _status_message = "Paused";
    } else if (event == AUDIO_PLAYER_CALLBACK_EVENT_UNKNOWN_FILE_TYPE) {
        playback().clearActivePath();
        _status_message = "Unsupported audio file";
    } else if (event == AUDIO_PLAYER_CALLBACK_EVENT_SHUTDOWN) {
        playback().clearActivePath();
        _status_message = "Audio stopped";
    }

    render();
}

void AppMp3Player::adjust_volume(int percentDelta)
{
    const int currentPercent = get_volume_percent();
    const int nextPercent    = std::clamp(currentPercent + percentDelta, MIN_VOLUME_PERCENT, 100);
    const int nextVolume     = (255 * nextPercent + 50) / 100;

    if (!GetHAL().setSpeakerVolume(nextVolume)) {
        _status_message = "Volume change failed";
        return;
    }

    _status_message = "Volume " + std::to_string(nextPercent) + "%";
}

void AppMp3Player::play_selected_file()
{
    if (_browser == nullptr) {
        return;
    }

    const auto* entry = _browser->getSelectedEntry();
    if (entry == nullptr) {
        _status_message = "No MP3 file selected";
        return;
    }

    if (entry->is_directory) {
        std::string errorMessage;
        if (!_browser->enterSelectedDirectory(errorMessage)) {
            _status_message = "Browse failed: " + errorMessage;
            return;
        }

        if (_browser->getEntryCount() == 0) {
            _status_message = "No MP3 files in " + _browser->getCurrentPath();
        } else {
            _status_message = "Enter Play  ;/. Move";
        }
        _browser->setStatusMessage(_status_message);
        return;
    }

    if (playback().isPlaying() && entry->path == playback().getActivePath()) {
        _stop_requested = true;
        playback().stop();
        _status_message = "Playback stopped";
        return;
    }

    std::string errorMessage;
    if (playback().playFile(entry->path, errorMessage)) {
        _stop_requested = false;
        _status_message = "Playing " + entry->name;
        return;
    }

    _status_message = "Play failed: " + errorMessage;
}

void AppMp3Player::stop_current_song()
{
    if (!playback().isPlaying()) {
        _status_message = "No song playing";
        return;
    }

    _stop_requested = true;
    playback().stop();
    _status_message = "Playback stopped";
}

void AppMp3Player::restart_current_song()
{
    const std::string path = playback().getActivePath();
    if (path.empty()) {
        _status_message = "No active song";
        return;
    }

    std::string errorMessage;
    if (!playback().playFile(path, errorMessage)) {
        _status_message = "Restart failed: " + errorMessage;
        return;
    }

    _stop_requested = false;
    _status_message = "Playing " + make_display_name(path);
    sync_browser_to_active_track();
}

void AppMp3Player::sync_browser_to_active_track()
{
    if (_browser == nullptr) {
        return;
    }

    const std::string activePath = playback().getActivePath();
    if (activePath.empty()) {
        refresh_browser();
        return;
    }

    std::string errorMessage;
    if (!_browser->focusPath(activePath, errorMessage)) {
        refresh_browser();
        if (_status_message.empty()) {
            _status_message = "Browse failed: " + errorMessage;
        }
        return;
    }

    _status_message = "Playing " + make_display_name(activePath);
    _browser->setStatusMessage(_status_message);
}

int AppMp3Player::get_volume_percent() const
{
    const int volume = GetHAL().getSpeakerVolume();
    return std::clamp((volume * 100 + 127) / 255, 0, 100);
}

AppMp3PlaybackService& AppMp3Player::playback()
{
    return AppMp3PlaybackService::instance();
}

const AppMp3PlaybackService& AppMp3Player::playback() const
{
    return AppMp3PlaybackService::instance();
}

std::string AppMp3Player::make_display_name(const std::string& path) const
{
    if (path.empty()) {
        return std::string();
    }

    const std::size_t separator = path.find_last_of('/');
    if (separator == std::string::npos || separator + 1 >= path.size()) {
        return path;
    }

    return path.substr(separator + 1);
}

std::string AppMp3Player::truncate_text(const std::string& value, std::size_t maxLength) const
{
    if (value.size() <= maxLength) {
        return value;
    }

    if (maxLength <= 3) {
        return value.substr(0, maxLength);
    }

    return value.substr(0, maxLength - 3) + "...";
}