/*
 * SPDX-FileCopyrightText: 2025 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "app_mp3_player_audio.h"

#include <string>

class AppMp3PlaybackService {
public:
    static AppMp3PlaybackService& instance();

    bool begin();
    void end();

    bool playFile(const std::string& path, std::string& errorMessage);
    void stop();

    bool isReady() const;
    bool isPlaying() const;
    const std::string& getActivePath() const;
    void clearActivePath();
    audio_player_callback_event_t consumeLastEvent();

private:
    AppMp3PlaybackService() = default;

    AppMp3PlayerAudio& ensureAudio();

    AppMp3PlayerAudio _audio;
};