/*
 * SPDX-FileCopyrightText: 2025 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include "app_config_editor.h"
#include <cstdint>
#include <hal/hal.h>
#include <memory>
#include <mooncake.h>

class AppConfig : public mooncake::AppAbility {
public:
    AppConfig();
    ~AppConfig();

    void onOpen() override;
    void onRunning() override;
    void onClose() override;

private:
    static constexpr const char* SETTINGS_CONFIG_PATH     = "/sdcard/settings.conf";
    static constexpr std::uint32_t CURSOR_BLINK_PERIOD_MS = 500;
    static constexpr int STATUS_BAR_HEIGHT                = 18;

    std::unique_ptr<AppConfigEditor> _editor;
    int _key_event_slot_id            = -1;
    bool _cursor_visible              = true;
    std::uint32_t _cursor_update_time = 0;

    void load_editor_content();
    void update_viewport_metrics();
    void render();
    void render_status_bar();
    void render_document();
    void render_cursor();
    void handle_key_event(const Keyboard::KeyEvent_t& keyEvent);
    void update_cursor();
    void save_file();
};