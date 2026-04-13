/*
 * SPDX-FileCopyrightText: 2025 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "hal.h"
#include <mooncake_log.h>

namespace {
constexpr char kHalTag[] = "HAL";
}  // namespace

bool Hal::setDisplayBrightness(int32_t brightness)
{
    if (brightness < 0 || brightness > 255) {
        mclog::tagWarn(kHalTag, "reject invalid display brightness: {}", brightness);
        return false;
    }

    _display_brightness = static_cast<uint8_t>(brightness);
    display.setBrightness(_display_brightness);
    return true;
}

void Hal::display_init()
{
    mclog::tagInfo(kHalTag, "display init");

    recreate_display_sprites();
}

void Hal::setSystemBarVisible(bool visible)
{
    if (_system_bar_visible == visible) {
        return;
    }

    _system_bar_visible = visible;
    recreate_display_sprites();
    display.clear();
}

void Hal::recreate_display_sprites()
{
    canvas.deleteSprite();
    canvasSystemBar.deleteSprite();

    const int canvasWidth     = display.width();
    const int appCanvasHeight = _system_bar_visible ? 109 : display.height();

    canvas.createSprite(canvasWidth, appCanvasHeight);

    if (_system_bar_visible) {
        canvasSystemBar.createSprite(canvas.width(), display.height() - canvas.height());
    }
}