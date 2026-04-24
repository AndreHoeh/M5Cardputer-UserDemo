#include "settings_config.h"
#include "hal.h"
#include <mooncake_log.h>
#include <array>
#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>

namespace settings_config {
namespace {
constexpr std::size_t SETTINGS_LINE_BUFFER_SIZE = 256;

std::string trim_copy(const std::string& value)
{
    const auto begin =
        std::find_if_not(value.begin(), value.end(), [](unsigned char ch) { return std::isspace(ch) != 0; });
    if (begin == value.end()) {
        return {};
    }

    const auto end =
        std::find_if_not(value.rbegin(), value.rend(), [](unsigned char ch) { return std::isspace(ch) != 0; }).base();
    return std::string(begin, end);
}

void discard_line_remainder(FILE* file)
{
    int ch = 0;
    while ((ch = fgetc(file)) != EOF && ch != '\n') {
    }
}

bool parse_int32_value(const std::string& raw_value, int32_t& parsed_value)
{
    errno                 = 0;
    char* end             = nullptr;
    const long long value = std::strtoll(raw_value.c_str(), &end, 10);
    if (end == raw_value.c_str() || errno != 0) {
        return false;
    }

    if (!trim_copy(end).empty()) {
        return false;
    }

    if (value < std::numeric_limits<int32_t>::min() || value > std::numeric_limits<int32_t>::max()) {
        return false;
    }

    parsed_value = static_cast<int32_t>(value);
    return true;
}

bool apply_speaker_volume_config(Hal& hal, int32_t value, size_t, const std::string&)
{
    return hal.setSpeakerVolume(value);
}

bool apply_display_brightness_config(Hal& hal, int32_t value, size_t, const std::string&)
{
    return hal.setDisplayBrightness(value);
}

bool apply_record_gain_config(Hal& hal, int32_t value, size_t line_number, const std::string& logTag)
{
    if (value < 16 || value > 255) {
        mclog::tagWarn(logTag, "reject invalid record gain on line {}: {}", line_number, value);
        return false;
    }

    hal.getSettings().SetInt("record_gain", value);
    return true;
}

bool apply_idle_sleep_timeout_config(Hal& hal, int32_t value, size_t line_number, const std::string& logTag)
{
    if (value < 0) {
        mclog::tagWarn(logTag, "reject invalid idle sleep timeout on line {}: {}", line_number, value);
        return false;
    }

    return hal.setIdleSleepTimeoutMs(static_cast<std::uint32_t>(value));
}

struct SettingsConfigHandlerEntry {
    const char* key;
    bool (*apply)(Hal& hal, int32_t value, size_t line_number, const std::string& logTag);
};

constexpr std::array<SettingsConfigHandlerEntry, 4> SETTINGS_CONFIG_HANDLERS = {{
    {"speaker_volume", &apply_speaker_volume_config},
    {"display_brightness", &apply_display_brightness_config},
    {"record_gain", &apply_record_gain_config},
    {"idle_sleep_timeout_ms", &apply_idle_sleep_timeout_config},
}};

const SettingsConfigHandlerEntry* find_settings_config_handler(const std::string& key)
{
    const auto it = std::find_if(SETTINGS_CONFIG_HANDLERS.begin(), SETTINGS_CONFIG_HANDLERS.end(),
                                 [&key](const SettingsConfigHandlerEntry& entry) { return key == entry.key; });
    if (it == SETTINGS_CONFIG_HANDLERS.end()) {
        return nullptr;
    }

    return &(*it);
}
}  // namespace

std::size_t loadFromFile(Hal& hal, const char* filePath, const std::string& logTag)
{
    FILE* config_file = fopen(filePath, "r");
    if (config_file == nullptr) {
        mclog::tagInfo(logTag, "skip SD settings config: failed to open {}: {}", filePath, std::strerror(errno));
        return 0;
    }

    char line_buffer[SETTINGS_LINE_BUFFER_SIZE];
    size_t line_number   = 0;
    size_t applied_count = 0;

    while (fgets(line_buffer, sizeof(line_buffer), config_file) != nullptr) {
        ++line_number;

        std::string line(line_buffer);
        const bool line_too_long = line.find('\n') == std::string::npos && !feof(config_file);
        if (line_too_long) {
            discard_line_remainder(config_file);
            mclog::tagWarn(logTag, "skip SD settings line {}: line too long", line_number);
            continue;
        }

        line.erase(std::remove(line.begin(), line.end(), '\r'), line.end());

        const auto comment_pos = line.find('#');
        if (comment_pos != std::string::npos) {
            line.erase(comment_pos);
        }

        line = trim_copy(line);
        if (line.empty()) {
            continue;
        }

        const auto separator_pos = line.find('=');
        if (separator_pos == std::string::npos) {
            mclog::tagWarn(logTag, "skip SD settings line {}: expected key=value", line_number);
            continue;
        }

        const std::string key   = trim_copy(line.substr(0, separator_pos));
        const std::string value = trim_copy(line.substr(separator_pos + 1));
        if (key.empty() || value.empty()) {
            mclog::tagWarn(logTag, "skip SD settings line {}: empty key or value", line_number);
            continue;
        }

        if (key == "config_version") {
            int32_t parsed_version = 0;
            if (!parse_int32_value(value, parsed_version)) {
                mclog::tagWarn(logTag, "skip SD settings line {}: invalid config_version '{}'", line_number, value);
                continue;
            }

            if (parsed_version != 1) {
                mclog::tagWarn(logTag, "unexpected SD settings config_version {} on line {}", parsed_version,
                               line_number);
            }
            continue;
        }

        int32_t parsed_value = 0;
        if (!parse_int32_value(value, parsed_value)) {
            mclog::tagWarn(logTag, "skip SD settings line {}: invalid integer '{}' for key '{}'", line_number, value,
                           key);
            continue;
        }

        const SettingsConfigHandlerEntry* handler = find_settings_config_handler(key);
        if (handler == nullptr) {
            mclog::tagWarn(logTag, "skip SD settings line {}: unknown key '{}'", line_number, key);
            continue;
        }

        const bool applied = handler->apply(hal, parsed_value, line_number, logTag);
        if (!applied) {
            mclog::tagWarn(logTag, "failed to apply SD setting '{}' from line {}", key, line_number);
            continue;
        }

        ++applied_count;
        mclog::tagInfo(logTag, "applied SD setting {}={}", key, parsed_value);
    }

    fclose(config_file);
    mclog::tagInfo(logTag, "finished SD settings config load, applied {} override(s)", applied_count);
    return applied_count;
}

}  // namespace settings_config