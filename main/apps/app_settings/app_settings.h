#pragma once
#include <mooncake.h>
#include "settings_registry.h"
#include <hal/hal.h>
#include <cstddef>
#include <string>

class AppSettings : public mooncake::AppAbility {
public:
    AppSettings();
    ~AppSettings();

    void onOpen() override;
    void onRunning() override;
    void onClose() override;

private:
    settings_model::SettingsRegistry _settings_registry;
    size_t _selected_setting_index = 0;

    std::string _volume_input;
    int _pending_volume         = 0;
    int _pre_edit_volume        = 0;
    bool _is_pending_valid      = false;
    bool _is_dirty              = false;
    bool _is_editing            = false;
    bool _needs_redraw          = false;
    bool _replace_on_next_digit = true;
    int _key_event_slot_id = -1;

    settings_model::Setting* current_setting();
    bool navigate_setting(int delta);
    bool apply_selected_setting_runtime(int value);
    void refresh_selected_setting_state();

    void render_interface();
    void render_selected_setting();
    void initialize_settings_model();
    void handle_key_event(const Keyboard::KeyEvent_t& keyEvent);
    void update_pending_volume_from_input();
    void update_dirty_state();
    void start_editing();
    void confirm_editing();
    void restore_pre_edit_volume();
    void save_to_nvs();
};
