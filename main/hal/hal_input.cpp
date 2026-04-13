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

void Hal::keyboard_init()
{
    mclog::tagInfo(kHalTag, "keyboard init");

    if (!keyboard.init()) {
        mclog::tagError(kHalTag, "keyboard init failed");
    }
}