#pragma once
#include <mooncake.h>
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
    void handle_key_event(const Keyboard::KeyEvent_t& keyEvent);
    void update_pending_volume_from_input();
    void update_dirty_state();
    void start_editing();
    void confirm_editing();
    void restore_pre_edit_volume();
    int read_persisted_volume();
};
