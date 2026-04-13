/*
 * SPDX-FileCopyrightText: 2025 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "hal.h"
#include <mooncake_log.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <esp_mac.h>
#include <memory>

namespace {
constexpr char kHalTag[] = "HAL";

std::unique_ptr<Hal> s_hal_instance;
}  // namespace

Hal& GetHAL()
{
    if (!s_hal_instance) {
        mclog::tagInfo(kHalTag, "creating hal instance");
        s_hal_instance = std::make_unique<Hal>();
    }
    return *s_hal_instance.get();
}

void Hal::init()
{
    mclog::tagInfo(kHalTag, "init");

    M5.begin();
    M5.Display.setBrightness(0);
    M5.Speaker.begin();

    display_init();
    i2c_scan();
    keyboard_init();
    setting_init();
    spi_init();

    _last_user_activity_ms = millis();
}

void Hal::update()
{
    M5.update();
    keyboard.update();
    capLora868.update();

    if (keyboard.getLatestKeyEvent().keyCode != KEY_NONE || homeButton.wasPressed()) {
        reportUserActivity();
    }
}

void Hal::feedTheDog()
{
    vTaskDelay(1);
}

bool Hal::setSpeakerVolume(int32_t volume)
{
    if (volume < 0 || volume > 255) {
        mclog::tagWarn(kHalTag, "reject invalid speaker volume: {}", volume);
        return false;
    }

    _speaker_volume = static_cast<uint8_t>(volume);
    speaker.setVolume(_speaker_volume);
    return true;
}

uint8_t Hal::getScaledSpeakerVolume(float scale) const
{
    const float clampedScale = std::clamp(scale, 0.0f, 1.0f);
    const float scaled       = static_cast<float>(_speaker_volume) * clampedScale;
    return static_cast<uint8_t>(std::lround(scaled));
}

void Hal::applyScaledSpeakerVolume(float scale)
{
    speaker.setVolume(getScaledSpeakerVolume(scale));
}

std::vector<uint8_t> Hal::getDeviceMac()
{
    std::vector<uint8_t> mac(6);
    esp_read_mac(mac.data(), ESP_MAC_EFUSE_FACTORY);
    return mac;
}

std::string Hal::getDeviceMacString()
{
    auto mac = getDeviceMac();
    return fmt::format("{:02X}:{:02X}:{:02X}:{:02X}:{:02X}:{:02X}", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

void Hal::reportUserActivity()
{
    _last_user_activity_ms = millis();
}

void Hal::i2c_scan()
{
    mclog::tagInfo(kHalTag, "i2c scan");

    bool ret[128] = {false};
    M5.In_I2C.scanID(ret);

    uint8_t address;
    printf("     0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f\r\n");
    for (int i = 0; i < 128; i += 16) {
        printf("%02x: ", i);
        for (int j = 0; j < 16; j++) {
            fflush(stdout);
            address = i + j;
            if (ret[address]) {
                printf("%02x ", address);
            } else {
                printf("-- ");
            }
        }
        printf("\r\n");
    }
}
