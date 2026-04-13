/*
 * SPDX-FileCopyrightText: 2025 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "hal.h"
#include "hal_config.h"
#include <mooncake_log.h>
#include <driver/gpio.h>
#include <esp_sleep.h>

namespace {
constexpr char kHalTag[] = "HAL";
}  // namespace

bool Hal::setIdleSleepTimeoutMs(std::uint32_t timeoutMs)
{
    if (timeoutMs > MAX_IDLE_SLEEP_TIMEOUT_MS) {
        mclog::tagWarn(kHalTag, "reject invalid idle sleep timeout: {}", static_cast<unsigned long>(timeoutMs));
        return false;
    }

    _idle_sleep_timeout_ms = timeoutMs;
    reportUserActivity();
    return true;
}

bool Hal::canEnterLightSleep() const
{
    if (_is_wifi_inited || _is_esp_now_inited || _is_ble_keyboard_inited || _is_usb_keyboard_inited) {
        return false;
    }

    if (capLora868.isInited()) {
        return false;
    }

    return true;
}

void Hal::checkAndEnterSleepIfIdle()
{
    if (!isIdleSleepEnabled()) {
        return;
    }

    const auto now = millis();
    if ((now - _last_user_activity_ms) < _idle_sleep_timeout_ms) {
        return;
    }

    if (!enterLightSleep()) {
        reportUserActivity();
    }
}

void Hal::updateWakeReason()
{
    switch (esp_sleep_get_wakeup_cause()) {
        case ESP_SLEEP_WAKEUP_TIMER:
            _last_wake_reason = SleepWakeReason::Timer;
            break;
        case ESP_SLEEP_WAKEUP_GPIO:
        case ESP_SLEEP_WAKEUP_EXT0:
        case ESP_SLEEP_WAKEUP_EXT1:
            _last_wake_reason = SleepWakeReason::Keyboard;
            break;
        case ESP_SLEEP_WAKEUP_UNDEFINED:
            _last_wake_reason = SleepWakeReason::None;
            break;
        default:
            _last_wake_reason = SleepWakeReason::Unknown;
            break;
    }
}

bool Hal::enterLightSleep(std::uint32_t timerWakeupMs)
{
    if (!canEnterLightSleep()) {
        mclog::tagDebug(kHalTag, "skip light sleep while peripheral modes are active");
        return false;
    }

    const std::uint32_t effective_timer_ms = (timerWakeupMs > 0) ? timerWakeupMs : _pending_sleep_timer_ms;
    const gpio_num_t keyboard_wake_pin     = static_cast<gpio_num_t>(HAL_PIN_KEYBOARD_INT);

    gpio_wakeup_disable(keyboard_wake_pin);
    ESP_ERROR_CHECK(esp_sleep_enable_gpio_wakeup());
    ESP_ERROR_CHECK(gpio_wakeup_enable(keyboard_wake_pin, GPIO_INTR_LOW_LEVEL));

    if (effective_timer_ms > 0) {
        ESP_ERROR_CHECK(esp_sleep_enable_timer_wakeup(static_cast<uint64_t>(effective_timer_ms) * 1000ULL));
    }

    display.setBrightness(0);

    const esp_err_t ret = esp_light_sleep_start();

    gpio_wakeup_disable(keyboard_wake_pin);
    display.setBrightness(_display_brightness);

    if (ret != ESP_OK) {
        mclog::tagError(kHalTag, "light sleep failed: {}", esp_err_to_name(ret));
        reportUserActivity();
        return false;
    }

    _pending_sleep_timer_ms = 0;
    updateWakeReason();
    reportUserActivity();
    return true;
}