/*
 * SPDX-FileCopyrightText: 2025 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "hal.h"
#include "hal_config.h"
#include "utils/ir_nec/ir_helper.h"
#include <mooncake_log.h>

namespace {
constexpr char kHalTag[] = "HAL";
}  // namespace

void Hal::irInit()
{
    mclog::tagInfo(kHalTag, "ir init");

    if (_is_ir_inited) {
        mclog::tagInfo(kHalTag, "ir already inited");
        return;
    }

    ir_helper_init(static_cast<gpio_num_t>(HAL_PIN_IR_TX));
    _is_ir_inited = true;
}

void Hal::irSend(uint8_t addr, uint8_t cmd)
{
    mclog::tagInfo(kHalTag, "ir send: addr: {:02X}, cmd: {:02X}", addr, cmd);

    if (!_is_ir_inited) {
        mclog::tagError(kHalTag, "ir not inited");
        return;
    }

    ir_helper_send(addr, cmd);
}