/*
 * SPDX-FileCopyrightText: 2025 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "app_mp3_player_audio.h"

#include <apps/utils/audio/audio.h>
#include <apps/utils/audio/speaker_arbiter.h>
#include <hal.h>
#include <mooncake_log.h>

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>

#include "freertos/task.h"

namespace {
constexpr char kAudioTag[] = "AppMp3Audio";

AppMp3PlayerAudio* g_active_audio = nullptr;
}  // namespace

AppMp3PlayerAudio::AppMp3PlayerAudio()
{
    _write_context.owner = this;
}

AppMp3PlayerAudio::~AppMp3PlayerAudio()
{
    end();
}

bool AppMp3PlayerAudio::begin()
{
    if (_started) {
        return true;
    }

    prepareSpeaker();

    audio_player_config_t config = {};
    config.mute_fn               = muteCallback;
    config.clk_set_fn            = nullptr;
    config.write_fn              = nullptr;
    config.priority              = 5;
    config.coreID                = tskNO_AFFINITY;
    config.force_stereo          = true;
    config.write_fn2             = writeCallback;
    config.write_ctx             = &_write_context;

    auto clockThunk = +[](std::uint32_t rate, std::uint32_t bitsCfg, i2s_slot_mode_t ch) -> esp_err_t {
        if (g_active_audio == nullptr) {
            return ESP_ERR_INVALID_STATE;
        }
        return g_active_audio->onClockConfig(rate, bitsCfg, ch);
    };
    config.clk_set_fn = clockThunk;

    g_active_audio = this;
    esp_err_t err  = audio_player_new(config);
    if (err != ESP_OK) {
        g_active_audio = nullptr;
        restoreSpeaker();
        mclog::tagWarn(kAudioTag, "audio_instance_new failed: {}", static_cast<int>(err));
        return false;
    }

    err = audio_player_callback_register(handleAudioEvent, this);
    if (err != ESP_OK) {
        mclog::tagWarn(kAudioTag, "audio_instance_callback_register failed: {}", static_cast<int>(err));
        audio_player_delete();
        g_active_audio = nullptr;
        restoreSpeaker();
        return false;
    }

    _last_event.store(AUDIO_PLAYER_CALLBACK_EVENT_IDLE);
    _started = true;
    return true;
}

void AppMp3PlayerAudio::end()
{
    if (!_started) {
        return;
    }

    stop();
    audio_player_delete();
    audio::set_speaker_sfx_suppressed(false);
    g_active_audio = nullptr;
    _active_path.clear();
    _started = false;
    restoreSpeaker();
}

bool AppMp3PlayerAudio::playFile(const std::string& path, std::string& errorMessage)
{
    if (!begin()) {
        errorMessage = "audio init failed";
        return false;
    }

    const bool wasPlaying      = isPlaying();
    const bool speakerAcquired = audio::try_acquire_speaker(audio::SpeakerOwner::Mp3Playback);
    if (!speakerAcquired) {
        errorMessage = std::string("audio busy: ") + audio::describe_speaker_owner(audio::current_speaker_owner());
        return false;
    }

    if (!wasPlaying) {
        captureSpeakerVolumeForPlayback();
    }

    FILE* file = fopen(path.c_str(), "rb");
    if (file == nullptr) {
        audio::release_speaker(audio::SpeakerOwner::Mp3Playback);
        if (!wasPlaying) {
            restoreSpeakerVolumeAfterPlayback();
        }
        errorMessage = std::strerror(errno);
        return false;
    }

    _write_context.nextBufferIndex = 0;
    esp_err_t err                  = audio_player_play(file);
    if (err != ESP_OK) {
        fclose(file);
        audio::release_speaker(audio::SpeakerOwner::Mp3Playback);
        if (!wasPlaying) {
            restoreSpeakerVolumeAfterPlayback();
        }
        errorMessage = "play request failed";
        return false;
    }

    _active_path = path;
    audio::set_speaker_sfx_suppressed(true);
    return true;
}

void AppMp3PlayerAudio::stop()
{
    if (_started) {
        audio_player_stop();
    }
    GetHAL().speaker.stop(SPEAKER_CHANNEL);
}

bool AppMp3PlayerAudio::isReady() const
{
    return _started;
}

bool AppMp3PlayerAudio::isPlaying() const
{
    if (!_started) {
        return false;
    }

    const audio_player_state_t state = audio_player_get_state();
    return state == AUDIO_PLAYER_STATE_PLAYING || state == AUDIO_PLAYER_STATE_PAUSE;
}

const std::string& AppMp3PlayerAudio::getActivePath() const
{
    return _active_path;
}

void AppMp3PlayerAudio::clearActivePath()
{
    _active_path.clear();
}

audio_player_callback_event_t AppMp3PlayerAudio::consumeLastEvent()
{
    const int previous = _last_event.exchange(AUDIO_PLAYER_CALLBACK_EVENT_UNKNOWN);
    return static_cast<audio_player_callback_event_t>(previous);
}

void AppMp3PlayerAudio::handleAudioEvent(audio_player_cb_ctx_t* ctx)
{
    if (ctx == nullptr || ctx->user_ctx == nullptr) {
        return;
    }

    auto* self = static_cast<AppMp3PlayerAudio*>(ctx->user_ctx);
    self->_last_event.store(ctx->audio_event);

    if (ctx->audio_event == AUDIO_PLAYER_CALLBACK_EVENT_IDLE ||
        ctx->audio_event == AUDIO_PLAYER_CALLBACK_EVENT_SHUTDOWN ||
        ctx->audio_event == AUDIO_PLAYER_CALLBACK_EVENT_UNKNOWN_FILE_TYPE) {
        audio::release_speaker(audio::SpeakerOwner::Mp3Playback);
        self->restoreSpeakerVolumeAfterPlayback();
        audio::set_speaker_sfx_suppressed(false);
    }
}

esp_err_t AppMp3PlayerAudio::muteCallback(AUDIO_PLAYER_MUTE_SETTING setting)
{
    if (g_active_audio == nullptr) {
        return ESP_OK;
    }

    if (setting == AUDIO_PLAYER_MUTE) {
        GetHAL().speaker.stop(SPEAKER_CHANNEL);
    }
    return ESP_OK;
}

esp_err_t AppMp3PlayerAudio::clockConfigCallback(std::uint32_t rate, std::uint32_t bitsCfg, i2s_slot_mode_t ch,
                                                 void* ctx)
{
    if (ctx == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }

    auto* self = static_cast<WriteContext*>(ctx)->owner;
    if (self == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }

    return self->onClockConfig(rate, bitsCfg, ch);
}

esp_err_t AppMp3PlayerAudio::writeCallback(void* audioBuffer, size_t len, size_t* bytesWritten, std::uint32_t timeoutMs,
                                           void* ctx)
{
    if (ctx == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }

    auto* self = static_cast<WriteContext*>(ctx)->owner;
    if (self == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }

    return self->onWrite(audioBuffer, len, bytesWritten, timeoutMs);
}

esp_err_t AppMp3PlayerAudio::onClockConfig(std::uint32_t rate, std::uint32_t bitsCfg, i2s_slot_mode_t ch)
{
    _write_context.sampleRate    = rate;
    _write_context.bitsPerSample = bitsCfg;
    _write_context.channels      = (ch == I2S_SLOT_MODE_MONO) ? 1U : 2U;

    if (bitsCfg != 8 && bitsCfg != 16) {
        mclog::tagWarn(kAudioTag, "unsupported bits per sample: {}", bitsCfg);
        return ESP_ERR_NOT_SUPPORTED;
    }

    return ESP_OK;
}

esp_err_t AppMp3PlayerAudio::onWrite(void* audioBuffer, size_t len, size_t* bytesWritten, std::uint32_t timeoutMs)
{
    if (audioBuffer == nullptr || bytesWritten == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }

    if (len > PCM_BUFFER_CAPACITY) {
        mclog::tagWarn(kAudioTag, "pcm chunk too large: {}", static_cast<unsigned>(len));
        return ESP_ERR_NO_MEM;
    }

    PcmBuffer& buffer = _write_context.buffers[_write_context.nextBufferIndex];
    std::memcpy(buffer.data.data(), audioBuffer, len);

    const TickType_t deadline =
        (timeoutMs == portMAX_DELAY) ? portMAX_DELAY : (xTaskGetTickCount() + pdMS_TO_TICKS(timeoutMs));

    bool queued = false;
    do {
        if (_write_context.bitsPerSample == 16) {
            queued = GetHAL().speaker.playRaw(reinterpret_cast<const int16_t*>(buffer.data.data()),
                                              len / sizeof(std::int16_t), _write_context.sampleRate,
                                              _write_context.channels > 1, 1, SPEAKER_CHANNEL, false);
        } else {
            queued = GetHAL().speaker.playRaw(buffer.data.data(), len, _write_context.sampleRate,
                                              _write_context.channels > 1, 1, SPEAKER_CHANNEL, false);
        }

        if (queued) {
            _write_context.nextBufferIndex = (_write_context.nextBufferIndex + 1) % PCM_BUFFER_COUNT;
            *bytesWritten                  = len;
            return ESP_OK;
        }

        if (deadline != portMAX_DELAY && xTaskGetTickCount() >= deadline) {
            break;
        }

        vTaskDelay(pdMS_TO_TICKS(1));
    } while (true);

    *bytesWritten = 0;
    return ESP_ERR_TIMEOUT;
}

void AppMp3PlayerAudio::prepareSpeaker()
{
    if (!audio::can_use_speaker(audio::SpeakerOwner::Mp3Playback)) {
        return;
    }

    _speaker_was_running = GetHAL().speaker.isRunning();
    if (!_speaker_was_running) {
        GetHAL().beginSpeakerOutput();
    }
    GetHAL().speaker.stop(SPEAKER_CHANNEL);
}

void AppMp3PlayerAudio::captureSpeakerVolumeForPlayback()
{
    if (_speaker_volume_saved) {
        return;
    }

    _speaker_volume_before = GetHAL().getSpeakerVolume();
    _speaker_volume_saved  = true;
}

void AppMp3PlayerAudio::restoreSpeakerVolumeAfterPlayback()
{
    if (!_speaker_volume_saved) {
        return;
    }

    GetHAL().setSpeakerVolume(_speaker_volume_before);
    _speaker_volume_saved = false;
}

void AppMp3PlayerAudio::restoreSpeaker()
{
    restoreSpeakerVolumeAfterPlayback();
    GetHAL().speaker.stop(SPEAKER_CHANNEL);
    if (!_speaker_was_running) {
        GetHAL().speaker.end();
    }
    audio::release_speaker(audio::SpeakerOwner::Mp3Playback);
}