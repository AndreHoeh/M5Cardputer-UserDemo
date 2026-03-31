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
#include <cstdio>
#include <cstdlib>

// #define NO_BOOT_PLAY

using namespace smooth_ui_toolkit;

void start_arkanoid();

static void fancy_logo_fade_in()
{
    GetHAL().display.pushImage(63, 32, 114, 62, image_data_logo_adv);

    AnimateValue brightness;
    brightness.easingOptions().duration = 600;

    GetHAL().display.setBrightness(0);
    brightness      = 255;
    auto time_count = GetHAL().millis();
    while (GetHAL().millis() - time_count < 1200) {
        GetHAL().feedTheDog();
        GetHAL().update();
        GetHAL().delay(10);

        GetHAL().display.setBrightness(brightness);
    }

    GetHAL().display.setBrightness(255);
}

void Launcher::boot_anim()
{
    mclog::tagInfo(getAppInfo().name, "start boot anim");

    // GetHAL().delay(300);  // Codec init takes some time

    // If software restart
    if (esp_reset_reason() != ESP_RST_POWERON) {
        mclog::tagInfo(getAppInfo().name, "not power on reset, skip boot anim");
        GetHAL().display.setBrightness(255);
        return;
    }

    fancy_logo_fade_in();

    // Show boot image
    GetHAL().display.pushImage(0, 0, 240, 135, image_data_logo);
    GetHAL().display.fillRect(195, 113, 40, 19, (uint32_t)0xE6E6E6);
    GetHAL().display.setFont(&fonts::efontCN_16);
    GetHAL().display.setTextColor((uint32_t)0x999999);
    GetHAL().display.drawString(FW_VERSION, 201, 109);

    // Play boot sfx from SD card: place boot_sfx.wav at the root of the SD card
    uint8_t* wav_buf = nullptr;
#ifndef NO_BOOT_PLAY
    {
        auto sd = GetHAL().sdCardProbe();
        if (sd.is_mounted) {
            FILE* fp = fopen("/sdcard/boot_sfx.wav", "rb");
            if (fp) {
                fseek(fp, 0, SEEK_END);
                size_t wav_size = static_cast<size_t>(ftell(fp));
                rewind(fp);
                wav_buf = static_cast<uint8_t*>(malloc(wav_size));
                if (wav_buf) {
                    fread(wav_buf, 1, wav_size, fp);
                    fclose(fp);
                    GetHAL().speaker.setVolume(50);
                    GetHAL().speaker.playWav(wav_buf, wav_size);
                } else {
                    mclog::tagWarn(getAppInfo().name, "boot sfx malloc failed");
                    fclose(fp);
                }
            } else {
                mclog::tagWarn(getAppInfo().name, "boot_sfx.wav not found on SD card");
            }
        } else {
            mclog::tagWarn(getAppInfo().name, "SD card not mounted, skipping boot sfx");
        }
    }
#endif

    // Wait enter
    int egg_count = 0;
    while (1) {
        GetHAL().feedTheDog();
        GetHAL().delay(50);
        GetHAL().update();

        if (GetHAL().homeButton.wasPressed()) {
            GetHAL().speaker.setVolume(90);
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
    if (wav_buf) {
        free(wav_buf);
    }
}