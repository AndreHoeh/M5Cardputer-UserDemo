#pragma once
#include <mooncake.h>

class AppSettings : public mooncake::AppAbility {
public:
    AppSettings();
    ~AppSettings();

    void onOpen() override;
    void onRunning() override;
    void onClose() override;

private:
    void render_interface();
    void render_volume_setting();
};
