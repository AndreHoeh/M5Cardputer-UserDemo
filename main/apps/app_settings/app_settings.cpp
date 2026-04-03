#include "app_settings.h"
#include <apps/utils/audio/audio.h>
#include <apps/utils/common.h>
#include <apps/utils/theme.h>
#include <hal.h>
#include <mooncake_log.h>
#include <algorithm>
#include <cctype>

using namespace mooncake;

namespace {
constexpr char SPEAKER_VOLUME_SETTING_KEY[] = "speaker_volume";
}

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

    _pending_volume        = GetHAL().getSpeakerVolume();
    _pre_edit_volume       = _pending_volume;
    _volume_input          = std::to_string(_pending_volume);
    _is_editing            = false;
    _replace_on_next_digit = true;
    _status_message        = "Press Enter to edit";
    _key_event_slot_id     = GetHAL().keyboard.onKeyEvent.connect(
        [this](const Keyboard::KeyEvent_t& keyEvent) { handle_key_event(keyEvent); });

    update_pending_volume_from_input();
    _needs_redraw = true;

    render_interface();
    GetHAL().pushCanvas();
    _needs_redraw = false;
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

    render_volume_setting();
}

void AppSettings::render_volume_setting()
{
    GetHAL().canvas.print("Volume : ");

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
        GetHAL().canvas.print(std::to_string(_pending_volume).c_str());
    }
    GetHAL().canvas.println();
    GetHAL().canvas.setTextColor(TFT_WHITE, THEME_COLOR_BG);

    GetHAL().canvas.setTextColor(TFT_CYAN, THEME_COLOR_BG);
    GetHAL().canvas.printf("%s\n", _status_message.c_str());
    GetHAL().canvas.setTextColor(TFT_WHITE, THEME_COLOR_BG);

    if (_is_editing) {
        GetHAL().canvas.println("Enter: confirm");
    } else {
        GetHAL().canvas.println("Enter: edit");
    }
    GetHAL().canvas.println("Opt+H: exit");
}

void AppSettings::handle_key_event(const Keyboard::KeyEvent_t& keyEvent)
{
    if (!keyEvent.state || keyEvent.isModifier) {
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

            if (_volume_input.length() >= 3) {
                _status_message = "Max 3 digits (0-255)";
                _needs_redraw   = true;
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
    if (_volume_input.empty()) {
        _is_pending_valid = false;
        _is_dirty         = false;

        if (_is_editing) {
            _status_message = "Empty value. Enter to restore";
        } else {
            _status_message = "Press Enter to edit";
        }
        return;
    }

    int value = 0;
    for (const char ch : _volume_input) {
        if (!std::isdigit(static_cast<unsigned char>(ch))) {
            _is_pending_valid = false;
            _is_dirty         = false;
            _status_message   = "Invalid input. Enter to restore";
            return;
        }

        value = value * 10 + static_cast<int>(ch - '0');
        if (value > 255) {
            _is_pending_valid = false;
            _is_dirty         = false;
            _status_message   = "Out of range. Enter to restore";
            return;
        }
    }

    _pending_volume   = value;
    _is_pending_valid = true;

    if (_is_editing) {
        GetHAL().setSpeakerVolume(static_cast<uint8_t>(_pending_volume), false);
    }

    update_dirty_state();

    if (_is_editing) {
        if (_is_dirty) {
            _status_message = "Edited value active (not saved)";
        } else {
            _status_message = "Matches saved value";
        }
    } else {
        _status_message = "Press Enter to edit";
    }
}

void AppSettings::update_dirty_state()
{
    _is_dirty = (_pending_volume != read_persisted_volume());
}

void AppSettings::start_editing()
{
    _is_editing            = true;
    _pre_edit_volume       = _pending_volume;
    _replace_on_next_digit = true;
    _status_message        = "Editing volume. Enter to confirm";
}

void AppSettings::confirm_editing()
{
    if (!_is_pending_valid || _volume_input.empty()) {
        restore_pre_edit_volume();
        _status_message = "Invalid input restored";
    } else {
        _volume_input = std::to_string(_pending_volume);

        if (_is_dirty) {
            _status_message = "Edited value active (not saved)";
        } else {
            _status_message = "Matches saved value";
        }
    }

    _is_editing            = false;
    _replace_on_next_digit = true;
}

void AppSettings::restore_pre_edit_volume()
{
    _pending_volume   = std::clamp(_pre_edit_volume, 0, 255);
    _volume_input     = std::to_string(_pending_volume);
    _is_pending_valid = true;

    GetHAL().setSpeakerVolume(static_cast<uint8_t>(_pending_volume), false);
    update_dirty_state();
}

int AppSettings::read_persisted_volume()
{
    const int persisted = GetHAL().getSettings().GetInt(SPEAKER_VOLUME_SETTING_KEY, GetHAL().getSpeakerVolume());
    return std::clamp(persisted, 0, 255);
}
