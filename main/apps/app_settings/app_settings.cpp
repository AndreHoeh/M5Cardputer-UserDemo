#include "app_settings.h"
#include <apps/utils/audio/audio.h>
#include <apps/utils/common.h>
#include <apps/utils/theme.h>
#include <hal.h>
#include <mooncake_log.h>
#include <cctype>

using namespace mooncake;

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
    _volume_input          = std::to_string(_pending_volume);
    _replace_on_next_digit = true;
    _status_message        = "Type 0-9 then Opt+S to save";
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
    const uint8_t saved_volume = GetHAL().getSpeakerVolume();

    GetHAL().canvas.printf("Saved Volume : %u\n", saved_volume);
    GetHAL().canvas.printf("Edit Volume  : %s\n", _volume_input.empty() ? "<empty>" : _volume_input.c_str());

    if (_is_pending_valid) {
        GetHAL().canvas.printf("Parsed Value : %d\n", _pending_volume);
    } else {
        GetHAL().canvas.setTextColor(TFT_RED, THEME_COLOR_BG);
        GetHAL().canvas.println("Parsed Value : INVALID (0-255)");
        GetHAL().canvas.setTextColor(TFT_WHITE, THEME_COLOR_BG);
    }

    if (_is_dirty) {
        GetHAL().canvas.setTextColor(TFT_YELLOW, THEME_COLOR_BG);
        GetHAL().canvas.println("Status       : UNSAVED");
    } else {
        GetHAL().canvas.setTextColor(TFT_GREEN, THEME_COLOR_BG);
        GetHAL().canvas.println("Status       : SAVED");
    }

    GetHAL().canvas.setTextColor(TFT_CYAN, THEME_COLOR_BG);
    GetHAL().canvas.printf("%s\n", _status_message.c_str());
    GetHAL().canvas.setTextColor(TFT_WHITE, THEME_COLOR_BG);

    GetHAL().canvas.println("Type digits 0-9 to edit");
    GetHAL().canvas.println("Backspace/Del: delete");
    GetHAL().canvas.println("Opt+S: save   Opt+H: exit");
}

void AppSettings::handle_key_event(const Keyboard::KeyEvent_t& keyEvent)
{
    if (!keyEvent.state || keyEvent.isModifier) {
        return;
    }

    if (keyEvent.keyCode == KEY_S && (GetHAL().keyboard.getModifierMask() & KEY_MOD_LMETA)) {
        save_pending_volume();
        _needs_redraw = true;
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
        _status_message   = "Enter a value between 0 and 255";
        return;
    }

    int value = 0;
    for (const char ch : _volume_input) {
        if (!std::isdigit(static_cast<unsigned char>(ch))) {
            _is_pending_valid = false;
            _is_dirty         = false;
            _status_message   = "Invalid input. Use digits only";
            return;
        }

        value = value * 10 + static_cast<int>(ch - '0');
        if (value > 255) {
            _is_pending_valid = false;
            _is_dirty         = false;
            _status_message   = "Out of range. Volume must be 0-255";
            return;
        }
    }

    _pending_volume   = value;
    _is_pending_valid = true;
    _is_dirty         = (_pending_volume != static_cast<int>(GetHAL().getSpeakerVolume()));

    if (_is_dirty) {
        _status_message = "Pending change. Press Opt+S to save";
    } else {
        _status_message = "Matches saved value";
    }
}

void AppSettings::save_pending_volume()
{
    if (!_is_pending_valid) {
        _status_message = "Save blocked: value must be 0-255";
        return;
    }

    if (!_is_dirty) {
        _status_message = "No change to save";
        return;
    }

    GetHAL().setSpeakerVolume(static_cast<uint8_t>(_pending_volume), true);
    _is_dirty              = false;
    _replace_on_next_digit = true;
    _status_message        = "Volume saved";
}
