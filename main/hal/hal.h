/*
 * SPDX-FileCopyrightText: 2025 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include "keyboard/keyboard.h"
#include "cap_lora868/cap_lora868.h"
#include "utils/settings/settings.h"
#include <M5Unified.hpp>
#include <M5GFX.h>
#include <algorithm>
#include <memory>
#include <cstdint>
#include <string>
#include <vector>

class Hal {
public:
    enum class SleepWakeReason {
        None = 0,
        Timer,
        Keyboard,
        Unknown,
    };

    void init();
    void update();

    /* --------------------------------- System --------------------------------- */
    void delay(std::uint32_t ms)
    {
        m5gfx::delay(ms);
    }
    std::uint32_t millis()
    {
        return m5gfx::millis();
    }
    void feedTheDog();
    std::vector<uint8_t> getDeviceMac();
    std::string getDeviceMacString();
    void reportUserActivity();
    void loadSettingsFromSdConfig();
    void checkAndEnterSleepIfIdle();
    bool enterLightSleep(std::uint32_t timerWakeupMs = 0);
    bool setIdleSleepTimeoutMs(std::uint32_t timeoutMs);
    std::uint32_t getIdleSleepTimeoutMs() const
    {
        return _idle_sleep_timeout_ms;
    }
    bool isIdleSleepEnabled() const
    {
        return _idle_sleep_timeout_ms > 0;
    }
    void setPendingSleepTimerMs(std::uint32_t durationMs)
    {
        _pending_sleep_timer_ms = durationMs;
    }
    void clearPendingSleepTimer()
    {
        _pending_sleep_timer_ms = 0;
    }
    std::uint32_t getPendingSleepTimerMs() const
    {
        return _pending_sleep_timer_ms;
    }
    SleepWakeReason getLastWakeReason() const
    {
        return _last_wake_reason;
    }

    /* --------------------------------- Display -------------------------------- */
    M5GFX& display                = M5.Display;
    LGFX_Sprite canvas            = LGFX_Sprite(&M5.Display);
    LGFX_Sprite canvasSystemBar   = LGFX_Sprite(&M5.Display);
    LGFX_Sprite canvasKeyboardBar = LGFX_Sprite(&M5.Display);

    void setSystemBarVisible(bool visible);
    bool isSystemBarVisible() const
    {
        return _system_bar_visible;
    }
    int getCanvasTopOffset() const
    {
        return _system_bar_visible ? canvasSystemBar.height() : 0;
    }

    inline void pushCanvasSystemBar()
    {
        if (_system_bar_visible && canvasSystemBar.width() > 0 && canvasSystemBar.height() > 0) {
            canvasSystemBar.pushSprite(canvasKeyboardBar.width(), 0);
        }
    }
    inline void pushCanvasKeyboardBar()
    {
        canvasKeyboardBar.pushSprite(0, 0);
    }
    inline void pushCanvas()
    {
        canvas.pushSprite(canvasKeyboardBar.width(), getCanvasTopOffset());
    }

    /* ---------------------------------- Audio --------------------------------- */
    m5::Speaker_Class& speaker = M5.Speaker;
    m5::Mic_Class& mic         = M5.Mic;

    uint8_t getSpeakerVolume() const
    {
        return _speaker_volume;
    }
    bool setSpeakerVolume(int32_t volume);
    uint8_t getScaledSpeakerVolume(float scale) const;
    void applyScaledSpeakerVolume(float scale);

    bool setDisplayBrightness(int32_t brightness);
    uint8_t getDisplayBrightness() const
    {
        return _display_brightness;
    }

    /* ---------------------------------- Input --------------------------------- */
    m5::Button_Class& homeButton = M5.BtnA;
    Keyboard keyboard;

    /* ---------------------------------- Power --------------------------------- */
    inline uint8_t getBatLevel()
    {
        return M5.Power.getBatteryLevel();
    }
    inline int16_t getBatVoltage()
    {
        return M5.Power.getBatteryVoltage();
    }

    /* ---------------------------------- WiFi ---------------------------------- */
    using ScanResult_t = std::pair<int, std::string>;
    void wifiInit();
    void wifiDeinit();
    void wifiScan(std::vector<ScanResult_t>& scanResult);
    bool wifiConnect(const std::string& ssid, const std::string& password);
    bool isWifiConnected() const
    {
        return _is_wifi_connected;
    }
    void wifiDisconnect();

    /* --------------------------------- EspNow --------------------------------- */
    void espNowInit();
    void espNowDeinit();
    void espNowSend(const std::string& data);
    bool espNowAvailable();
    const std::string& espNowGetReceivedData();
    void espNowClearReceivedData();

    /* ----------------------------------- IR ----------------------------------- */
    void irInit();
    void irSend(uint8_t addr, uint8_t cmd);

    /* ----------------------------------- BLE ---------------------------------- */
    void bleKeyboardInit();
    bool bleKeyboardIsConnected() const;

    /* ----------------------------------- USB ---------------------------------- */
    void usbKeyboardInit();
    bool usbKeyboardIsConnected() const;

    /* -------------------------------- Settings -------------------------------- */
    Settings& getSettings()
    {
        return *_settings;
    }

    /* ----------------------------------- IMU ---------------------------------- */
    m5::IMU_Class& imu = M5.Imu;

    /* --------------------------------- SD Card -------------------------------- */
    struct SdCardProbeResult_t {
        bool is_mounted = false;
        std::string size;
        std::string type;
        std::string name;

        bool operator==(const SdCardProbeResult_t& other) const
        {
            return is_mounted == other.is_mounted && size == other.size && type == other.type && name == other.name;
        }
    };

    SdCardProbeResult_t sdCardProbe();

    /* ----------------------------------- Cap ---------------------------------- */
    CapLoRa868 capLora868;

private:
    static constexpr int DISPLAY_CANVAS_WIDTH                    = 204;
    static constexpr uint8_t DEFAULT_SPEAKER_VOLUME              = 30;
    static constexpr uint8_t DEFAULT_DISPLAY_BRIGHTNESS          = 255;
    static constexpr std::uint32_t DEFAULT_IDLE_SLEEP_TIMEOUT_MS = 0;
    static constexpr std::uint32_t MAX_IDLE_SLEEP_TIMEOUT_MS     = 60U * 60U * 1000U;

    Settings* _settings                   = nullptr;
    uint8_t _speaker_volume               = DEFAULT_SPEAKER_VOLUME;
    uint8_t _display_brightness           = DEFAULT_DISPLAY_BRIGHTNESS;
    bool _is_wifi_inited                  = false;
    bool _is_wifi_connected               = false;
    bool _is_esp_now_inited               = false;
    bool _is_ir_inited                    = false;
    bool _is_ble_keyboard_inited          = false;
    bool _is_usb_keyboard_inited          = false;
    bool _is_sd_card_mounted              = false;
    bool _is_sd_settings_loaded           = false;
    std::uint32_t _idle_sleep_timeout_ms  = DEFAULT_IDLE_SLEEP_TIMEOUT_MS;
    std::uint32_t _last_user_activity_ms  = 0;
    std::uint32_t _pending_sleep_timer_ms = 0;
    SleepWakeReason _last_wake_reason     = SleepWakeReason::None;
    bool _system_bar_visible              = true;
    int _ble_keyboard_event_slot_id       = -1;
    int _usb_keyboard_event_slot_id       = -1;
    std::unique_ptr<CapLoRa868> _cap_lora868;

    void display_init();
    void recreate_display_sprites();
    void i2c_scan();
    void keyboard_init();
    void start_sntp();
    void stop_sntp();
    void setting_init();
    void spi_init();
    void sd_card_init();
    bool canEnterLightSleep() const;
    void updateWakeReason();
    void handle_ble_keyboard_event(const Keyboard::KeyEvent_t& keyEvent);
    void handle_usb_keyboard_event(const Keyboard::KeyEvent_t& keyEvent);
};

Hal& GetHAL();
