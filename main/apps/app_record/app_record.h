/*
 * SPDX-FileCopyrightText: 2025 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <mooncake.h>
#include <array>
#include <cstdint>
#include <cstdio>
#include <hal/hal.h>

/**
 * @brief
 *
 */
class AppRecord : public mooncake::AppAbility {
public:
    AppRecord();
    ~AppRecord();

    void onOpen() override;
    void onRunning() override;
    void onClose() override;

private:
    static constexpr size_t RECORD_CHUNK_SAMPLES      = 200;
    static constexpr size_t RECORD_SAMPLERATE         = 16000;
    static constexpr int32_t RECORD_GAIN_DEFAULT      = 16;
    static constexpr int32_t RECORD_GAIN_MIN          = 1;
    static constexpr int32_t RECORD_GAIN_MAX          = 32;
    static constexpr int32_t RECORD_GAIN_STEP         = 1;
    static constexpr uint32_t RECORD_FLUSH_INTERVALMS = 1000;
    static constexpr int16_t RECORD_CLIP_THRESHOLD    = 30000;

    uint32_t _record_start_ms    = 0;
    uint32_t _last_flush_ms      = 0;
    uint32_t _data_bytes_written = 0;
    int32_t _record_gain         = RECORD_GAIN_DEFAULT;
    int16_t _last_peak_level     = 0;
    bool _is_recording           = false;
    std::FILE* _record_file      = nullptr;
    std::array<int16_t, RECORD_CHUNK_SAMPLES> _record_chunk{};
    std::array<char, 96> _current_file_path{};
    std::array<char, 48> _current_file_name{};
    std::array<char, 64> _status_message{};

    void render_page();
    void render_waveform(int32_t top, int32_t height);
    void handle_key_event(const Keyboard::KeyEvent_t& key_event);
    bool handle_record_chunk();
    bool start_recording();
    bool stop_recording(const char* status_message = nullptr);
    bool ensure_recordings_directory();
    bool open_record_file();
    bool finalize_record_file();
    bool write_wav_header(uint32_t data_size_bytes);
    bool make_record_path(char* path, size_t path_size, char* file_name, size_t file_name_size);
    int32_t load_record_gain() const;
    bool set_record_gain(int32_t gain, bool persist);
    bool is_system_time_valid() const;
    void clear_status_message();
    void set_status_message(const char* message);
    void set_audio_busy_status_message();
};
