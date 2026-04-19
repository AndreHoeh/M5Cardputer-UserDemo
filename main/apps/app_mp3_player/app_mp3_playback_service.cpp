/*
 * SPDX-FileCopyrightText: 2025 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "app_mp3_playback_service.h"

AppMp3PlaybackService& AppMp3PlaybackService::instance()
{
    static AppMp3PlaybackService service;
    return service;
}

bool AppMp3PlaybackService::begin()
{
    return ensureAudio().begin();
}

void AppMp3PlaybackService::end()
{
    _audio.end();
}

bool AppMp3PlaybackService::playFile(const std::string& path, std::string& errorMessage)
{
    return ensureAudio().playFile(path, errorMessage);
}

void AppMp3PlaybackService::stop()
{
    _audio.stop();
}

bool AppMp3PlaybackService::isReady() const
{
    return _audio.isReady();
}

bool AppMp3PlaybackService::isPlaying() const
{
    return _audio.isPlaying();
}

const std::string& AppMp3PlaybackService::getActivePath() const
{
    return _audio.getActivePath();
}

void AppMp3PlaybackService::clearActivePath()
{
    _audio.clearActivePath();
}

audio_player_callback_event_t AppMp3PlaybackService::consumeLastEvent()
{
    return _audio.consumeLastEvent();
}

AppMp3PlayerAudio& AppMp3PlaybackService::ensureAudio()
{
    return _audio;
}