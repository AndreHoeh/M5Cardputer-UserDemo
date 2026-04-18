/*
 * SPDX-FileCopyrightText: 2025 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "hal.h"
#include "utils/settings_config/settings_config.h"
#include <mooncake_log.h>
#include <nvs_flash.h>

namespace {
constexpr char kHalTag[]             = "HAL";
constexpr char kSettingsConfigPath[] = "/sd/settings.conf";
}  // namespace

void Hal::setting_init()
{
    mclog::tagInfo(kHalTag, "setting init");

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        mclog::tagWarn(kHalTag, "erasing NVS flash to fix corruption");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    _settings = new Settings("cardputer", true);

    setSpeakerVolume(DEFAULT_SPEAKER_VOLUME);
    setDisplayBrightness(DEFAULT_DISPLAY_BRIGHTNESS);
    setIdleSleepTimeoutMs(DEFAULT_IDLE_SLEEP_TIMEOUT_MS);
}

void Hal::loadSettingsFromSdConfig()
{
    mclog::tagInfo(kHalTag, "load SD settings config");

    if (_is_sd_settings_loaded) {
        mclog::tagInfo(kHalTag, "skip SD settings config: already processed");
        return;
    }

    sd_card_init();

    if (!_is_sd_card_mounted) {
        mclog::tagInfo(kHalTag, "skip SD settings config: SD card not mounted");
        return;
    }

    _is_sd_settings_loaded = true;

    settings_config::loadFromFile(*this, kSettingsConfigPath, kHalTag);
}