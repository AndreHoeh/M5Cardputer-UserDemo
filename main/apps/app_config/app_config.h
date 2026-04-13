/*
 * SPDX-FileCopyrightText: 2025 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include "app_config_file_browser.h"
#include "app_config_editor.h"
#include <cstdint>
#include <hal/hal.h>
#include <memory>
#include <mooncake.h>
#include <string>

class AppConfig : public mooncake::AppAbility {
public:
    AppConfig();
    ~AppConfig();

    void onOpen() override;
    void onRunning() override;
    void onClose() override;

private:
    static constexpr std::uint32_t CURSOR_BLINK_PERIOD_MS = 500;
    static constexpr int STATUS_BAR_HEIGHT                = 18;

    enum class ViewState {
        Browser,
        CreateFile,
        Editor,
    };

    std::unique_ptr<AppConfigEditor> _editor;
    std::unique_ptr<AppConfigFileBrowser> _browser;
    int _key_event_slot_id            = -1;
    bool _cursor_visible              = true;
    std::uint32_t _cursor_update_time = 0;
    ViewState _state                  = ViewState::Browser;
    std::string _active_file_path;
    std::string _create_file_name;

    void load_editor_content(const std::string& filePath);
    void refresh_file_browser();
    void open_selected_file();
    void delete_selected_file();
    void abort_editor();
    void begin_create_file();
    void cancel_create_file();
    void commit_create_file();
    void update_viewport_metrics();
    void render();
    void render_status_bar();
    void render_browser();
    void render_document();
    void render_cursor();
    void handle_browser_key_event(const Keyboard::KeyEvent_t& keyEvent, uint8_t modifierMask);
    void handle_create_file_key_event(const Keyboard::KeyEvent_t& keyEvent, uint8_t modifierMask);
    void handle_editor_key_event(const Keyboard::KeyEvent_t& keyEvent, uint8_t modifierMask);
    void handle_key_event(const Keyboard::KeyEvent_t& keyEvent);
    void update_cursor();
    void save_file();
};