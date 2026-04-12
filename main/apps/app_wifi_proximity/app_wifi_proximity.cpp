/*
 * SPDX-FileCopyrightText: 2025 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "app_wifi_proximity.h"
#include "../app_wifi_scan/assets/scan_big.h"
#include "../app_wifi_scan/assets/scan_small.h"
#include <apps/utils/audio/audio.h>
#include <apps/utils/common.h>
#include <apps/utils/theme.h>
#include <mooncake_log.h>
#include <algorithm>

using namespace mooncake;

AppWifiProximity::AppWifiProximity()
{
    setAppInfo().name     = "Nearby";
    setAppInfo().userData = new AppIcon_t(image_data_scan_big, image_data_scan_small, false);
}

AppWifiProximity::~AppWifiProximity()
{
    delete static_cast<AppIcon_t*>(getAppInfo().userData);
}

void AppWifiProximity::onOpen()
{
    mclog::tagInfo(getAppInfo().name, "on open");

    _devices.clear();
    _last_alarm_ms_by_mac.clear();
    _last_render_ms    = 0;
    _last_snapshot_ms  = 0;
    _paused            = false;
    _scan_start_failed = false;

    _key_event_slot_id = GetHAL().keyboard.onKeyEvent.connect(
        [this](const Keyboard::KeyEvent_t& keyEvent) { handle_key_event(keyEvent); });

    if (!GetHAL().wifiProximityScanStart()) {
        _scan_start_failed = true;
    }

    render_page();
}

void AppWifiProximity::onRunning()
{
    if (!_scan_start_failed && !_paused) {
        GetHAL().wifiProximityScanPoll();
    }

    if (!_scan_start_failed && !_paused && (GetHAL().millis() - _last_snapshot_ms) >= SNAPSHOT_INTERVAL_MS) {
        refresh_snapshot();
        _last_snapshot_ms = GetHAL().millis();
    }

    if ((GetHAL().millis() - _last_render_ms) >= RENDER_INTERVAL_MS) {
        render_page();
        _last_render_ms = GetHAL().millis();
    }

    if (is_app_exit_requested()) {
        audio::play_random_tone();
        close();
    }
}

void AppWifiProximity::onClose()
{
    mclog::tagInfo(getAppInfo().name, "on close");

    if (_key_event_slot_id >= 0) {
        GetHAL().keyboard.onKeyEvent.disconnect(_key_event_slot_id);
        _key_event_slot_id = -1;
    }

    if (GetHAL().isWifiProximityScanActive()) {
        GetHAL().wifiProximityScanStop();
    }
}

void AppWifiProximity::handle_key_event(const Keyboard::KeyEvent_t& keyEvent)
{
    if (!keyEvent.state || keyEvent.isModifier) {
        return;
    }

    switch (keyEvent.keyCode) {
        case KEY_ENTER:
            set_paused(!_paused);
            break;
        case KEY_A:
            _alarm_enabled = !_alarm_enabled;
            break;
        case KEY_SEMICOLON:
            _alarm_threshold_dbm = std::min(_alarm_threshold_dbm + 5, MAX_ALARM_THRESHOLD_DBM);
            break;
        case KEY_DOT:
            _alarm_threshold_dbm = std::max(_alarm_threshold_dbm - 5, MIN_ALARM_THRESHOLD_DBM);
            break;
        default:
            return;
    }

    render_page();
}

void AppWifiProximity::render_page()
{
    GetHAL().canvas.fillScreen(THEME_COLOR_BG);
    GetHAL().canvas.setFont(FONT_BASIC);
    GetHAL().canvas.setTextSize(1);
    GetHAL().canvas.setTextScroll(false);
    GetHAL().canvas.setCursor(0, 0);

    GetHAL().canvas.setTextColor(TFT_CYAN, THEME_COLOR_BG);
    GetHAL().canvas.printf("PROX %s %ddBm ", _alarm_enabled ? "ALM" : "MUTE", _alarm_threshold_dbm);

    GetHAL().canvas.setTextColor(TFT_LIGHTGREY, THEME_COLOR_BG);
    GetHAL().canvas.printf("%s  seen:%u\n", _paused ? "PAUSED" : "RUNNING", static_cast<unsigned>(_devices.size()));
    GetHAL().canvas.setFont(FONT_SMALL);
    GetHAL().canvas.println("Enter=pause A=alarm\nUp/Down=threshold");
    GetHAL().canvas.setFont(FONT_BASIC);

    if (_scan_start_failed) {
        GetHAL().canvas.setTextColor(TFT_RED, THEME_COLOR_BG);
        GetHAL().canvas.println("Scan init failed");
        GetHAL().canvas.println("Close app and retry");
        GetHAL().pushCanvas();
        return;
    }

    if (_devices.empty()) {
        GetHAL().canvas.setTextColor(TFT_ORANGE, THEME_COLOR_BG);
        GetHAL().canvas.println("Listening for nearby WiFi");
        GetHAL().canvas.println("devices...");
        GetHAL().pushCanvas();
        return;
    }

    int shown = 0;
    for (const auto& device : _devices) {
        if (shown >= 5) {
            break;
        }

        if (device.rssi >= -55) {
            GetHAL().canvas.setTextColor(TFT_GREEN, THEME_COLOR_BG);
        } else if (device.rssi >= -67) {
            GetHAL().canvas.setTextColor(TFT_YELLOW, THEME_COLOR_BG);
        } else {
            GetHAL().canvas.setTextColor(TFT_RED, THEME_COLOR_BG);
        }

        GetHAL().canvas.printf("%3d %-4s %s\n", device.rssi, proximity_label(device.rssi), device.macString.c_str());
        shown++;
    }

    GetHAL().pushCanvas();
}

void AppWifiProximity::refresh_snapshot()
{
    GetHAL().wifiProximityScanGetDevices(_devices);
    update_alarm_state();
}

void AppWifiProximity::update_alarm_state()
{
    if (!_alarm_enabled) {
        return;
    }

    const auto now = GetHAL().millis();
    for (auto it = _last_alarm_ms_by_mac.begin(); it != _last_alarm_ms_by_mac.end();) {
        if ((now - it->second) > (ALARM_COOLDOWN_MS * 3)) {
            it = _last_alarm_ms_by_mac.erase(it);
        } else {
            ++it;
        }
    }

    for (const auto& device : _devices) {
        if (device.rssi < _alarm_threshold_dbm) {
            continue;
        }

        if ((now - device.firstSeenMs) > 2500) {
            continue;
        }

        const auto found = _last_alarm_ms_by_mac.find(device.macString);
        if (found != _last_alarm_ms_by_mac.end() && (now - found->second) < ALARM_COOLDOWN_MS) {
            continue;
        }

        audio::play_tone(1480, 0.05);
        _last_alarm_ms_by_mac[device.macString] = now;
        break;
    }
}

bool AppWifiProximity::set_paused(bool paused)
{
    if (paused == _paused) {
        return true;
    }

    if (paused) {
        if (GetHAL().isWifiProximityScanActive()) {
            GetHAL().wifiProximityScanStop();
        }
        _paused = true;
        return true;
    }

    _scan_start_failed = !GetHAL().wifiProximityScanStart();
    _paused            = _scan_start_failed;
    if (!_scan_start_failed) {
        _devices.clear();
        _last_snapshot_ms = 0;
    }
    return !_scan_start_failed;
}

const char* AppWifiProximity::proximity_label(int rssi) const
{
    if (rssi >= -50) {
        return "CLOSE";
    }
    if (rssi >= -60) {
        return "NEAR";
    }
    if (rssi >= -70) {
        return "MID";
    }
    return "FAR";
}
