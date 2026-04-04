#pragma once
#include <mooncake.h>
#include "settings_registry.h"
#include <hal/hal.h>
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
    settings_model::Setting* _volume_setting     = nullptr;
    settings_model::Setting* _brightness_setting = nullptr;

    std::string _volume_input;
    int _pending_volume         = 0;
    int _pre_edit_volume        = 0;
    bool _is_pending_valid      = false;
    bool _is_dirty              = false;
    bool _is_editing            = false;
    bool _needs_redraw          = false;
    bool _replace_on_next_digit = true;
    std::string _status_message;
    int _key_event_slot_id = -1;

    void render_interface();
    void render_volume_setting();
    void initialize_settings_model();
    void handle_key_event(const Keyboard::KeyEvent_t& keyEvent);
    void update_pending_volume_from_input();
    void update_dirty_state();
    void start_editing();
    void confirm_editing();
    void restore_pre_edit_volume();
    void save_to_nvs();
};
