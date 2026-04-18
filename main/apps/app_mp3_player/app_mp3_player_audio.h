/*
 * SPDX-FileCopyrightText: 2025 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>

#include <audio_player.h>

class AppMp3PlayerAudio {
public:
    AppMp3PlayerAudio();
    ~AppMp3PlayerAudio();

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
    static constexpr int SPEAKER_CHANNEL             = 0;
    static constexpr std::size_t PCM_BUFFER_COUNT    = 3;
    static constexpr std::size_t PCM_BUFFER_CAPACITY = 12 * 1024;

    struct PcmBuffer {
        std::array<std::uint8_t, PCM_BUFFER_CAPACITY> data{};
    };

    struct WriteContext {
        AppMp3PlayerAudio* owner    = nullptr;
        std::uint32_t sampleRate    = 44100;
        std::uint32_t bitsPerSample = 16;
        std::uint32_t channels      = 2;
        std::size_t nextBufferIndex = 0;
        std::array<PcmBuffer, PCM_BUFFER_COUNT> buffers{};
    };

    std::atomic<int> _last_event{AUDIO_PLAYER_CALLBACK_EVENT_UNKNOWN};
    std::string _active_path;
    WriteContext _write_context;
    bool _speaker_was_running = false;
    bool _started             = false;

    static void handleAudioEvent(audio_player_cb_ctx_t* ctx);
    static esp_err_t muteCallback(AUDIO_PLAYER_MUTE_SETTING setting);
    static esp_err_t clockConfigCallback(std::uint32_t rate, std::uint32_t bitsCfg, i2s_slot_mode_t ch, void* ctx);
    static esp_err_t writeCallback(void* audioBuffer, size_t len, size_t* bytesWritten, std::uint32_t timeoutMs,
                                   void* ctx);

    esp_err_t onClockConfig(std::uint32_t rate, std::uint32_t bitsCfg, i2s_slot_mode_t ch);
    esp_err_t onWrite(void* audioBuffer, size_t len, size_t* bytesWritten, std::uint32_t timeoutMs);
    void prepareSpeaker();
    void restoreSpeaker();
};