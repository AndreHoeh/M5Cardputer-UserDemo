/*
 * SPDX-FileCopyrightText: 2025 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "../../app_launcher.h"
#include "assets/logo_adv.h"
#include "assets/logo.h"
#include <apps/utils/common.h>
#include <apps/utils/audio/audio.h>
#include <smooth_ui_toolkit.h>
#include <mooncake_log.h>
#include <hal.h>
#include <apps/workers/sd_audio_worker.h>

using namespace mooncake;
using namespace smooth_ui_toolkit;

void start_arkanoid();

namespace {
constexpr char DISPLAY_BRIGHTNESS_SETTING_KEY[] = "disp_brightness";
constexpr int DEFAULT_DISPLAY_BRIGHTNESS        = 255;

uint8_t get_startup_brightness()
{
    const int32_t stored = GetHAL().getSettings().GetInt(DISPLAY_BRIGHTNESS_SETTING_KEY, DEFAULT_DISPLAY_BRIGHTNESS);
    return static_cast<uint8_t>(std::clamp<int32_t>(stored, 0, 255));
}
}  // namespace

static void fancy_logo_fade_in(uint8_t target_brightness)
{
    GetHAL().display.pushImage(63, 32, 114, 62, image_data_logo_adv);

    AnimateValue brightness;
    brightness.easingOptions().duration = 600;

    GetHAL().display.setBrightness(0);
    brightness      = target_brightness;
    auto time_count = GetHAL().millis();
    while (GetHAL().millis() - time_count < 1200) {
        GetHAL().feedTheDog();
        GetHAL().update();
        GetHAL().delay(10);

        GetHAL().display.setBrightness(brightness);
    }

    GetHAL().display.setBrightness(target_brightness);
}

void Launcher::boot_anim()
{
    mclog::tagInfo(getAppInfo().name, "start boot anim");
    const uint8_t startup_brightness = get_startup_brightness();

    // GetHAL().delay(300);  // Codec init takes some time

    // If software restart
    if (esp_reset_reason() != ESP_RST_POWERON) {
        mclog::tagInfo(getAppInfo().name, "not power on reset, skip boot anim");
        GetHAL().display.setBrightness(startup_brightness);
        return;
    }

    fancy_logo_fade_in(startup_brightness);

    // Show boot image
    GetHAL().display.pushImage(0, 0, 240, 135, image_data_logo);
    GetHAL().display.fillRect(195, 113, 40, 19, (uint32_t)0xE6E6E6);
    GetHAL().display.setFont(&fonts::efontCN_16);
    GetHAL().display.setTextColor((uint32_t)0x999999);
    GetHAL().display.drawString(FW_VERSION, 201, 109);

    // Play boot sfx from SD card.
    const int sfx_id =
        GetMooncake().createExtension(std::make_unique<workers::SdAudioWorker>("/sdcard/boot_sfx.wav", 50, 0));

    // Wait enter
    int egg_count = 0;
    while (1) {
        GetHAL().feedTheDog();
        GetHAL().delay(50);
        GetHAL().update();
        GetMooncake().extensionManager()->updateAbilities();

        if (GetHAL().homeButton.wasPressed()) {
            GetHAL().applyScaledSpeakerVolume(1.0f);
            audio::play_random_tone();
            break;
        }
        auto key_event = GetHAL().keyboard.getLatestKeyEvent();

        if (!key_event.state && key_event.keyCode == KEY_G) {
            for (int i = 0; i < 3; i++) {
                GetHAL().delay(40);
                audio::play_random_tone();
            }
            egg_count++;
            if (egg_count > 2) {
                while (1) {
                    start_arkanoid();
                }
            }

        } else if (!key_event.state && key_event.keyCode != KEY_NONE) {
            break;
        }
    }

    GetHAL().keyboard.clearKeyEvent();
    if (sfx_id >= 0) {
        GetMooncake().destroyExtension(sfx_id);
        GetMooncake().extensionManager()->updateAbilities();
    }
}