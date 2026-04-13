/*
 * SPDX-FileCopyrightText: 2025 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "hal.h"
#include "utils/ble_hid_device/ble_hid_device_helper.h"
#include "utils/tusb_hid_device/tusb_hid_device_helper.h"
#include <mooncake_log.h>
#include <cstring>

namespace {
constexpr char kHalTag[] = "HAL";
}  // namespace

void Hal::bleKeyboardInit()
{
    if (_is_ble_keyboard_inited) {
        mclog::tagWarn(kHalTag, "ble keyboard already initialized");
        return;
    }

    mclog::tagInfo(kHalTag, "ble keyboard init");

    ble_hid_device_helper_init();
    _ble_keyboard_event_slot_id = keyboard.onKeyEvent.connect(
        [this](const Keyboard::KeyEvent_t& keyEvent) { handle_ble_keyboard_event(keyEvent); });

    _is_ble_keyboard_inited = true;
    mclog::tagInfo(kHalTag, "ble keyboard init done, auto-forwarding enabled");
}

bool Hal::bleKeyboardIsConnected() const
{
    if (!_is_ble_keyboard_inited) {
        return false;
    }

    auto state = ble_hid_device_helper_get_state();
    return state == BLE_HID_DEVICE_STATE_CONNECTED;
}

void Hal::handle_ble_keyboard_event(const Keyboard::KeyEvent_t& keyEvent)
{
    if (!bleKeyboardIsConnected()) {
        return;
    }

    uint8_t buffer[8] = {0};
    if (keyEvent.state) {
        const uint8_t modifierMask = keyboard.getModifierMask();
        buffer[0]                  = modifierMask;

        if (!keyEvent.isModifier) {
            buffer[2] = keyEvent.keyCode;
        }

        ble_hid_device_helper_send(buffer);
        mclog::tagDebug(kHalTag, "ble keyboard sent key: {} (code: {}, modifier: 0x{:02x})",
                        keyEvent.keyName ? keyEvent.keyName : "special", static_cast<int>(keyEvent.keyCode),
                        modifierMask);
        return;
    }

    if (keyEvent.isModifier) {
        buffer[0] = keyboard.getModifierMask();
    } else {
        std::memset(buffer, 0, sizeof(buffer));
    }

    ble_hid_device_helper_send(buffer);
    mclog::tagDebug(kHalTag, "ble keyboard key released");
}

void Hal::usbKeyboardInit()
{
    if (_is_usb_keyboard_inited) {
        mclog::tagWarn(kHalTag, "usb keyboard already initialized");
        return;
    }

    mclog::tagInfo(kHalTag, "usb keyboard init");

    delay(200);
    tusb_hid_device_helper_init();

    _usb_keyboard_event_slot_id = keyboard.onKeyEvent.connect(
        [this](const Keyboard::KeyEvent_t& keyEvent) { handle_usb_keyboard_event(keyEvent); });

    _is_usb_keyboard_inited = true;
}

bool Hal::usbKeyboardIsConnected() const
{
    if (!_is_usb_keyboard_inited) {
        return false;
    }

    return tusb_hid_device_helper_is_mounted();
}

void Hal::handle_usb_keyboard_event(const Keyboard::KeyEvent_t& keyEvent)
{
    if (!usbKeyboardIsConnected()) {
        return;
    }

    if (keyEvent.state) {
        uint8_t keycode[6] = {keyEvent.keyCode};
        tusb_hid_device_helper_report(keyboard.getModifierMask(), keycode);
        mclog::tagDebug(kHalTag, "usb keyboard sent key: {} (code: {})",
                        keyEvent.keyName ? keyEvent.keyName : "special", static_cast<int>(keyEvent.keyCode));
        return;
    }

    tusb_hid_device_helper_report(0, nullptr);
    mclog::tagDebug(kHalTag, "usb keyboard key released");
}