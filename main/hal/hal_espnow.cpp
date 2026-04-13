/*
 * SPDX-FileCopyrightText: 2025 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "hal.h"
#include <mooncake_log.h>
#include <cinttypes>
#include <esp_check.h>
#include <esp_mac.h>
#include <espnow.h>
#include <espnow_storage.h>
#include <espnow_utils.h>

namespace {
constexpr char kHalTag[] = "HAL";

std::string s_espnow_received_data;

esp_err_t handle_espnow_received(uint8_t* src_addr, void* data, size_t size, wifi_pkt_rx_ctrl_t* rx_ctrl)
{
    const char* TAG = "espnow";

    ESP_PARAM_CHECK(src_addr);
    ESP_PARAM_CHECK(data);
    ESP_PARAM_CHECK(size);
    ESP_PARAM_CHECK(rx_ctrl);

    static uint32_t count = 0;

    ESP_LOGI(TAG, "espnow_recv, <%" PRIu32 "> [" MACSTR "][%d][%d][%u]: %.*s", count++, MAC2STR(src_addr),
             rx_ctrl->channel, rx_ctrl->rssi, size, size, static_cast<char*>(data));

    s_espnow_received_data = std::string(static_cast<char*>(data), size);
    return ESP_OK;
}
}  // namespace

void Hal::espNowInit()
{
    mclog::tagInfo(kHalTag, "esp now init");

    if (!_is_wifi_inited) {
        wifiInit();
    }

    if (_is_wifi_connected) {
        wifiDisconnect();
        espNowDeinit();
    }

    if (_is_esp_now_inited) {
        mclog::tagInfo(kHalTag, "esp now already inited");
        return;
    }

    espnow_config_t espnow_config = ESPNOW_INIT_CONFIG_DEFAULT();
    espnow_init(&espnow_config);
    espnow_set_config_for_data_type(ESPNOW_DATA_TYPE_DATA, true, handle_espnow_received);

    _is_esp_now_inited = true;
}

void Hal::espNowDeinit()
{
    mclog::tagInfo(kHalTag, "esp now deinit");

    if (!_is_esp_now_inited) {
        mclog::tagInfo(kHalTag, "esp now not inited");
        return;
    }

    espnow_deinit();
    _is_esp_now_inited = false;
}

void Hal::espNowSend(const std::string& data)
{
    mclog::tagInfo(kHalTag, "esp now send: {}", data);

    if (!_is_esp_now_inited) {
        mclog::tagError(kHalTag, "esp now not inited");
        return;
    }

    espnow_frame_head_t frame_head = ESPNOW_FRAME_CONFIG_DEFAULT();
    auto ret = espnow_send(ESPNOW_DATA_TYPE_DATA, ESPNOW_ADDR_BROADCAST, data.c_str(), data.size(), &frame_head,
                           portMAX_DELAY);
    if (ret != ESP_OK) {
        mclog::tagError(kHalTag, "failed to send esp now: {}", esp_err_to_name(ret));
    }
}

bool Hal::espNowAvailable()
{
    return !s_espnow_received_data.empty();
}

const std::string& Hal::espNowGetReceivedData()
{
    return s_espnow_received_data;
}

void Hal::espNowClearReceivedData()
{
    s_espnow_received_data.clear();
}