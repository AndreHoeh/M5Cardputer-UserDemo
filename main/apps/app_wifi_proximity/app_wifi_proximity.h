/*
 * SPDX-FileCopyrightText: 2025 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <mooncake.h>
#include <cstdint>
#include <map>
#include <vector>
#include <hal/hal.h>

class AppWifiProximity : public mooncake::AppAbility {
public:
    AppWifiProximity();
    ~AppWifiProximity();

    void onOpen() override;
    void onRunning() override;
    void onClose() override;

private:
    static constexpr std::uint32_t RENDER_INTERVAL_MS   = 300;
    static constexpr std::uint32_t SNAPSHOT_INTERVAL_MS = 500;
    static constexpr std::uint32_t ALARM_COOLDOWN_MS    = 10000;
    static constexpr int DEFAULT_ALARM_THRESHOLD_DBM    = -65;
    static constexpr int MIN_ALARM_THRESHOLD_DBM        = -90;
    static constexpr int MAX_ALARM_THRESHOLD_DBM        = -35;

    std::vector<Hal::ProximityScanResult_t> _devices;
    std::map<std::string, std::uint32_t> _last_alarm_ms_by_mac;
    std::uint32_t _last_render_ms   = 0;
    std::uint32_t _last_snapshot_ms = 0;
    int _key_event_slot_id          = -1;
    int _alarm_threshold_dbm        = DEFAULT_ALARM_THRESHOLD_DBM;
    bool _alarm_enabled             = true;
    bool _paused                    = false;
    bool _scan_start_failed         = false;

    void handle_key_event(const Keyboard::KeyEvent_t& keyEvent);
    void render_page();
    void refresh_snapshot();
    void update_alarm_state();
    bool set_paused(bool paused);
    const char* proximity_label(int rssi) const;
};
