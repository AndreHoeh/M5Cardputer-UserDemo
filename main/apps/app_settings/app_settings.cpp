#include "app_settings.h"
#include <apps/utils/audio/audio.h>
#include <apps/utils/common.h>
#include <apps/utils/theme.h>
#include <hal.h>
#include <mooncake_log.h>
#include <algorithm>
#include <cctype>
#include <limits>

using namespace mooncake;

namespace {
constexpr size_t NVS_KEY_MAX_LEN                = 15;
constexpr char SPEAKER_VOLUME_SETTING_KEY[]     = "speaker_volume";
constexpr char DISPLAY_BRIGHTNESS_SETTING_KEY[] = "disp_brightness";
constexpr int DEFAULT_DISPLAY_BRIGHTNESS        = 255;

static_assert(sizeof(SPEAKER_VOLUME_SETTING_KEY) - 1 <= NVS_KEY_MAX_LEN, "NVS key is too long: speaker volume");
static_assert(sizeof(DISPLAY_BRIGHTNESS_SETTING_KEY) - 1 <= NVS_KEY_MAX_LEN, "NVS key is too long: display brightness");

int32_t get_setting_min_int(const settings_model::Setting& setting)
{
    return setting.definition().range.int_min.value_or(std::numeric_limits<int32_t>::min());
}

int32_t get_setting_max_int(const settings_model::Setting& setting)
{
    return setting.definition().range.int_max.value_or(std::numeric_limits<int32_t>::max());
}

void log_status(const std::string& tag, const char* message)
{
    mclog::tagInfo(tag, "{}", message);
}

void log_status(const std::string& tag, const std::string& message)
{
    mclog::tagInfo(tag, "{}", message.c_str());
}
}  // namespace

AppSettings::AppSettings()
{
    setAppInfo().name     = "Settings";
    setAppInfo().userData = nullptr;
}

AppSettings::~AppSettings()
{
}

void AppSettings::onOpen()
{
    mclog::tagInfo(getAppInfo().name, "on open");

    initialize_settings_model();
    _settings_registry.loadAll(GetHAL().getSettings());

    if (auto* volume_setting = _settings_registry.findByKey(SPEAKER_VOLUME_SETTING_KEY)) {
        int32_t volume = GetHAL().getSpeakerVolume();
        if (volume_setting->getInt(volume)) {
            GetHAL().setSpeakerVolume(static_cast<uint8_t>(std::clamp(static_cast<int>(volume), 0, 255)), false);
        }
    }

    if (auto* brightness_setting = _settings_registry.findByKey(DISPLAY_BRIGHTNESS_SETTING_KEY)) {
        int32_t brightness_value = DEFAULT_DISPLAY_BRIGHTNESS;
        if (brightness_setting->getInt(brightness_value)) {
            const int clamped_brightness = std::clamp(static_cast<int>(brightness_value), 0, 255);
            GetHAL().display.setBrightness(static_cast<uint8_t>(clamped_brightness));
        }
    }

    _selected_setting_index = 0;
    _is_editing             = false;
    _replace_on_next_digit  = true;
    refresh_selected_setting_state();
    _key_event_slot_id = GetHAL().keyboard.onKeyEvent.connect(
        [this](const Keyboard::KeyEvent_t& keyEvent) { handle_key_event(keyEvent); });

    _needs_redraw = true;

    render_interface();
    GetHAL().pushCanvas();
    _needs_redraw = false;
}

settings_model::Setting* AppSettings::current_setting()
{
    if (_settings_registry.size() == 0) {
        return nullptr;
    }

    if (_selected_setting_index >= _settings_registry.size()) {
        _selected_setting_index = 0;
    }

    return _settings_registry.at(_selected_setting_index);
}

bool AppSettings::navigate_setting(int delta)
{
    const size_t count = _settings_registry.size();
    if (count == 0 || delta == 0) {
        return false;
    }

    int next = static_cast<int>(_selected_setting_index);
    next += delta;
    while (next < 0) {
        next += static_cast<int>(count);
    }
    next %= static_cast<int>(count);

    _selected_setting_index = static_cast<size_t>(next);
    _is_editing             = false;
    _replace_on_next_digit  = true;
    refresh_selected_setting_state();
    return true;
}

bool AppSettings::apply_selected_setting_runtime(int value)
{
    auto* setting = current_setting();
    if (setting == nullptr) {
        return false;
    }

    const auto& key         = setting->definition().key;
    const int clamped_value = std::clamp(value, 0, 255);

    if (key == SPEAKER_VOLUME_SETTING_KEY) {
        GetHAL().setSpeakerVolume(static_cast<uint8_t>(clamped_value), false);
        return true;
    }

    if (key == DISPLAY_BRIGHTNESS_SETTING_KEY) {
        GetHAL().display.setBrightness(static_cast<uint8_t>(clamped_value));
        return true;
    }

    return false;
}

void AppSettings::refresh_selected_setting_state()
{
    auto* setting = current_setting();
    if (setting == nullptr) {
        _pending_volume  = 0;
        _pre_edit_volume = 0;
        _volume_input.clear();
        _is_pending_valid = false;
        _is_dirty         = false;
        log_status(getAppInfo().name, "No settings available");
        return;
    }

    if (setting->definition().type != settings_model::SettingType::kInt) {
        _pending_volume  = 0;
        _pre_edit_volume = 0;
        _volume_input.clear();
        _is_pending_valid = false;
        update_dirty_state();
        log_status(getAppInfo().name, "Unsupported type in current UI");
        return;
    }

    int32_t value = 0;
    if (!setting->getInt(value)) {
        const int32_t min_value = get_setting_min_int(*setting);
        const int32_t max_value = get_setting_max_int(*setting);

        if (min_value > max_value) {
            _pending_volume  = 0;
            _pre_edit_volume = 0;
            _volume_input.clear();
            _is_pending_valid = false;
            _is_dirty         = false;
            log_status(getAppInfo().name, "Invalid setting range");
            return;
        }

        value = std::clamp<int32_t>(0, min_value, max_value);
        if (!setting->setInt(value)) {
            _pending_volume  = 0;
            _pre_edit_volume = 0;
            _volume_input.clear();
            _is_pending_valid = false;
            update_dirty_state();
            log_status(getAppInfo().name, setting->validationMessage());
            return;
        }
    }

    _pending_volume        = static_cast<int>(value);
    _pre_edit_volume       = _pending_volume;
    _volume_input          = std::to_string(_pending_volume);
    _is_pending_valid      = true;
    _replace_on_next_digit = true;
    update_dirty_state();

    if (!_is_editing) {
        log_status(getAppInfo().name, "Press Enter to edit");
    }
}

void AppSettings::initialize_settings_model()
{
    if (_settings_registry.size() == 0) {
        settings_model::SettingDefinition volume_setting;
        volume_setting.key           = SPEAKER_VOLUME_SETTING_KEY;
        volume_setting.name          = "Volume";
        volume_setting.type          = settings_model::SettingType::kInt;
        volume_setting.default_value = static_cast<int32_t>(GetHAL().getSpeakerVolume());
        volume_setting.range.int_min = 0;
        volume_setting.range.int_max = 255;
        _settings_registry.registerSetting(std::move(volume_setting));

        settings_model::SettingDefinition brightness_setting;
        brightness_setting.key           = DISPLAY_BRIGHTNESS_SETTING_KEY;
        brightness_setting.name          = "Brightness";
        brightness_setting.type          = settings_model::SettingType::kInt;
        brightness_setting.default_value = static_cast<int32_t>(DEFAULT_DISPLAY_BRIGHTNESS);
        brightness_setting.range.int_min = 0;
        brightness_setting.range.int_max = 255;
        _settings_registry.registerSetting(std::move(brightness_setting));
    }
}

void AppSettings::onRunning()
{
    if (is_app_exit_requested()) {
        audio::play_random_tone();
        close();
        return;
    }

    if (_needs_redraw) {
        render_interface();
        GetHAL().pushCanvas();
        _needs_redraw = false;
    }
}

void AppSettings::onClose()
{
    if (_key_event_slot_id >= 0) {
        GetHAL().keyboard.onKeyEvent.disconnect(_key_event_slot_id);
        _key_event_slot_id = -1;
    }

    mclog::tagInfo(getAppInfo().name, "on close");
}

void AppSettings::render_interface()
{
    GetHAL().canvas.fillScreen(THEME_COLOR_BG);
    GetHAL().canvas.setTextScroll(true);
    GetHAL().canvas.setBaseColor(THEME_COLOR_BG);
    GetHAL().canvas.setTextColor(TFT_WHITE, THEME_COLOR_BG);
    GetHAL().canvas.setTextSize(1);
    GetHAL().canvas.setCursor(0, 0);

    GetHAL().canvas.setTextColor(TFT_ORANGE, THEME_COLOR_BG);
    GetHAL().canvas.println("Settings");
    GetHAL().canvas.setTextColor(TFT_WHITE, THEME_COLOR_BG);
    GetHAL().canvas.println();

    render_selected_setting();
}

void AppSettings::render_selected_setting()
{
    auto* setting               = current_setting();
    const size_t total_settings = _settings_registry.size();

    if (setting == nullptr) {
        GetHAL().canvas.setTextColor(TFT_RED, THEME_COLOR_BG);
        GetHAL().canvas.println("No settings registered");
        GetHAL().canvas.setTextColor(TFT_WHITE, THEME_COLOR_BG);
        GetHAL().canvas.println("Opt+H: exit");
        return;
    }

    GetHAL().canvas.printf("Page %u/%u\n", static_cast<unsigned>(_selected_setting_index + 1),
                           static_cast<unsigned>(total_settings));
    GetHAL().canvas.printf("%s : ", setting->definition().name.c_str());

    if (_is_editing && !_is_pending_valid) {
        GetHAL().canvas.setTextColor(TFT_RED, THEME_COLOR_BG);
    } else if (_is_editing || _is_dirty) {
        GetHAL().canvas.setTextColor(TFT_YELLOW, THEME_COLOR_BG);
    } else {
        GetHAL().canvas.setTextColor(TFT_GREEN, THEME_COLOR_BG);
    }

    if (_is_editing) {
        GetHAL().canvas.print(_volume_input.empty() ? "<empty>" : _volume_input.c_str());
        GetHAL().canvas.print("_");
    } else {
        if (setting->definition().type == settings_model::SettingType::kInt) {
            GetHAL().canvas.print(std::to_string(_pending_volume).c_str());
        } else {
            GetHAL().canvas.print("<unsupported>");
        }
    }
    GetHAL().canvas.println();
    GetHAL().canvas.setTextColor(TFT_WHITE, THEME_COLOR_BG);

    if (_is_editing) {
        GetHAL().canvas.println("Enter: confirm");
    } else if (setting->definition().type == settings_model::SettingType::kInt) {
        GetHAL().canvas.println("Enter: edit");
    } else {
        GetHAL().canvas.println("Enter: not supported");
    }
    GetHAL().canvas.println("Opt+S: save  Opt+H: exit");
}

void AppSettings::handle_key_event(const Keyboard::KeyEvent_t& keyEvent)
{
    if (!keyEvent.state || keyEvent.isModifier) {
        return;
    }

    const bool navigate_left  = (keyEvent.keyCode == KEY_COMMA);  // red left arrow on keyboard
    const bool navigate_right = (keyEvent.keyCode == KEY_SLASH);  // red right arrow on keyboard
    if (navigate_left || navigate_right) {
        if (_is_editing) {
            confirm_editing();
        }

        if (!navigate_setting(navigate_left ? -1 : 1)) {
            log_status(getAppInfo().name, "Navigation failed");
        }
        _needs_redraw = true;
        return;
    }

    if (keyEvent.keyCode == KEY_ENTER) {
        if (_is_editing) {
            confirm_editing();
        } else {
            start_editing();
        }
        _needs_redraw = true;
        return;
    }

    if (keyEvent.keyCode == KEY_S && (GetHAL().keyboard.getModifierMask() & KEY_MOD_LMETA)) {
        save_to_nvs();
        _needs_redraw = true;
        return;
    }

    if (!_is_editing) {
        return;
    }

    bool changed = false;

    if (keyEvent.keyCode == KEY_BACKSPACE || keyEvent.keyCode == KEY_DELETE) {
        if (!_volume_input.empty()) {
            _volume_input.pop_back();
            _replace_on_next_digit = _volume_input.empty();
            changed                = true;
        }
    } else {
        char digit = '\0';

        // Prefer keyName so external keyboards and key variants still map to digits reliably.
        if (keyEvent.keyName && keyEvent.keyName[0] != '\0' && keyEvent.keyName[1] == '\0' &&
            std::isdigit(static_cast<unsigned char>(keyEvent.keyName[0]))) {
            digit = keyEvent.keyName[0];
        }

        if (digit != '\0') {
            if (_replace_on_next_digit) {
                _volume_input.clear();
                _replace_on_next_digit = false;
            }

            if (_volume_input.length() >= 10) {
                log_status(getAppInfo().name, "Max 10 digits");
                _needs_redraw = true;
                return;
            }

            _volume_input.push_back(digit);
            changed = true;
        }
    }

    if (!changed) {
        return;
    }

    update_pending_volume_from_input();
    _needs_redraw = true;
}

void AppSettings::update_pending_volume_from_input()
{
    auto* setting = current_setting();
    if (setting == nullptr) {
        _is_pending_valid = false;
        _is_dirty         = false;
        log_status(getAppInfo().name, "No setting selected");
        return;
    }

    if (setting->definition().type != settings_model::SettingType::kInt) {
        _is_pending_valid = false;
        _is_dirty         = setting->isDirty();
        log_status(getAppInfo().name, "Current type not editable");
        return;
    }

    if (_volume_input.empty()) {
        _is_pending_valid = false;
        update_dirty_state();

        if (_is_editing) {
            log_status(getAppInfo().name, "Empty value. Enter to restore");
        } else {
            log_status(getAppInfo().name, "Press Enter to edit");
        }
        return;
    }

    int64_t value = 0;
    for (const char ch : _volume_input) {
        if (!std::isdigit(static_cast<unsigned char>(ch))) {
            _is_pending_valid = false;
            update_dirty_state();
            log_status(getAppInfo().name, "Invalid input. Enter to restore");
            return;
        }

        value = value * 10 + static_cast<int64_t>(ch - '0');
        if (value > std::numeric_limits<int32_t>::max()) {
            _is_pending_valid = false;
            update_dirty_state();
            log_status(getAppInfo().name, "Value too large. Enter to restore");
            return;
        }
    }

    const int32_t min_value = get_setting_min_int(*setting);
    const int32_t max_value = get_setting_max_int(*setting);
    if (min_value > max_value) {
        _is_pending_valid = false;
        update_dirty_state();
        log_status(getAppInfo().name, "Invalid setting range");
        return;
    }

    if (value < min_value || value > max_value) {
        _is_pending_valid = false;
        update_dirty_state();
        log_status(getAppInfo().name, "Out of range. Enter to restore");
        return;
    }

    _pending_volume   = static_cast<int>(value);
    _is_pending_valid = true;

    if (!setting->setInt(static_cast<int32_t>(_pending_volume))) {
        _is_pending_valid = false;
        update_dirty_state();
        log_status(getAppInfo().name, setting->validationMessage());
        return;
    }

    if (_is_editing) {
        if (!apply_selected_setting_runtime(_pending_volume)) {
            log_status(getAppInfo().name, "Runtime apply not supported");
        }
    }

    update_dirty_state();

    if (_is_editing) {
        if (_is_dirty) {
            log_status(getAppInfo().name, "Edited value active (not saved)");
        } else {
            log_status(getAppInfo().name, "Matches saved value");
        }
    } else {
        log_status(getAppInfo().name, "Press Enter to edit");
    }
}

void AppSettings::update_dirty_state()
{
    auto* setting = current_setting();
    if (setting == nullptr) {
        _is_dirty = false;
        return;
    }

    _is_dirty = setting->isDirty();
}

void AppSettings::start_editing()
{
    auto* setting = current_setting();
    if (setting == nullptr) {
        log_status(getAppInfo().name, "No setting selected");
        return;
    }

    if (setting->definition().type != settings_model::SettingType::kInt) {
        _is_editing = false;
        log_status(getAppInfo().name, "Current type not editable");
        _needs_redraw = true;
        return;
    }

    _is_editing            = true;
    _pre_edit_volume       = _pending_volume;
    _replace_on_next_digit = true;
    log_status(getAppInfo().name, "Editing value. Enter to confirm");
}

void AppSettings::confirm_editing()
{
    if (!_is_pending_valid || _volume_input.empty()) {
        restore_pre_edit_volume();
        log_status(getAppInfo().name, "Invalid input restored");
    } else {
        _volume_input = std::to_string(_pending_volume);

        if (_is_dirty) {
            log_status(getAppInfo().name, "Edited value active (not saved)");
        } else {
            log_status(getAppInfo().name, "Matches saved value");
        }
    }

    _is_editing            = false;
    _replace_on_next_digit = true;
}

void AppSettings::restore_pre_edit_volume()
{
    auto* setting = current_setting();
    if (setting == nullptr) {
        return;
    }

    const int32_t min_value = get_setting_min_int(*setting);
    const int32_t max_value = get_setting_max_int(*setting);

    _pending_volume   = std::clamp(_pre_edit_volume, static_cast<int>(min_value), static_cast<int>(max_value));
    _volume_input     = std::to_string(_pending_volume);
    _is_pending_valid = true;

    if (!setting->setInt(static_cast<int32_t>(_pending_volume))) {
        _is_pending_valid = false;
        update_dirty_state();
        log_status(getAppInfo().name, setting->validationMessage());
        return;
    }

    if (!apply_selected_setting_runtime(_pending_volume)) {
        log_status(getAppInfo().name, "Runtime apply not supported");
    }
    update_dirty_state();
}

void AppSettings::save_to_nvs()
{
    auto* setting = current_setting();
    if (setting == nullptr) {
        log_status(getAppInfo().name, "No setting selected");
        return;
    }

    if (!_is_pending_valid || (_is_editing && _volume_input.empty())) {
        log_status(getAppInfo().name, "Save blocked: invalid value");
        return;
    }

    if (!setting->isDirty()) {
        log_status(getAppInfo().name, "Already saved");
        return;
    }

    if (!setting->save(GetHAL().getSettings())) {
        log_status(getAppInfo().name, "Save failed");
        update_dirty_state();
        return;
    }

    update_dirty_state();
    log_status(getAppInfo().name, "Saved current setting");
}
