#include "app_settings.h"
#include <apps/utils/audio/audio.h>
#include <apps/utils/theme.h>
#include <hal.h>
#include <mooncake_log.h>

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
    render_interface();
    GetHAL().pushCanvas();
}

void AppSettings::onRunning()
{
    if (GetHAL().homeButton.wasClicked()) {
        audio::play_random_tone();
        close();
    }
}

void AppSettings::onClose()
{
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
    const uint8_t volume = GetHAL().getSpeakerVolume();
    mclog::tagInfo(getAppInfo().name, "speaker volume: {}", volume);
    GetHAL().canvas.printf("Speaker Volume: %u\n", volume);
    GetHAL().canvas.println("Press Home to exit");
}
