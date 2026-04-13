/*
 * SPDX-FileCopyrightText: 2025 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "hal.h"
#include <mooncake_log.h>
#include <algorithm>
#include <cassert>
#include <cstring>
#include <esp_err.h>
#include <esp_event.h>
#include <esp_netif.h>
#include <esp_sntp.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <esp_wifi.h>
#include <lwip/err.h>
#include <lwip/sys.h>
#include <nvs.h>
#include <nvs_flash.h>
#include <sys/time.h>
#include <time.h>

namespace {
constexpr char kHalTag[]                       = "HAL";
constexpr std::size_t kDefaultScanListSize     = 6;
constexpr uint8_t kProximityScanFirstChannel   = 1;
constexpr uint8_t kProximityScanLastChannel    = 13;
constexpr uint32_t kProximityScanHopIntervalMs = 225;
constexpr uint32_t kProximityScanStaleMs       = 12000;

wifi_ap_record_t s_ap_info[kDefaultScanListSize];
EventGroupHandle_t s_wifi_event_group = NULL;

static const int WIFI_CONNECTED_BIT    = BIT0;
static const int WIFI_DISCONNECTED_BIT = BIT1;
static const int WIFI_FAIL_BIT         = BIT2;
static const int WIFI_STARTED_BIT      = BIT3;

#pragma pack(push, 1)
struct WifiMacHeader {
    uint16_t frameControl;
    uint16_t duration;
    uint8_t receiver[6];
    uint8_t transmitter[6];
    uint8_t bssid[6];
    uint16_t sequenceControl;
};
#pragma pack(pop)

wifi_promiscuous_filter_t s_proximity_filter = {
    .filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT | WIFI_PROMIS_FILTER_MASK_DATA,
};

bool s_proximity_scan_active     = false;
uint8_t s_proximity_channel      = kProximityScanFirstChannel;
uint32_t s_proximity_next_hop_ms = 0;
std::vector<Hal::ProximityScanResult_t> s_proximity_results;

uint32_t proximity_now_ms()
{
    return static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);
}

std::string format_mac_string(const uint8_t mac[6])
{
    return fmt::format("{:02X}:{:02X}:{:02X}:{:02X}:{:02X}:{:02X}", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

bool should_track_client_frame(const WifiMacHeader& header, wifi_promiscuous_pkt_type_t type)
{
    const uint16_t frameControl = header.frameControl;
    const uint8_t frameSubtype  = static_cast<uint8_t>((frameControl >> 4) & 0x0f);
    const bool toDs             = (frameControl & 0x0100U) != 0;
    const bool fromDs           = (frameControl & 0x0200U) != 0;

    if (type == WIFI_PKT_MGMT) {
        return frameSubtype == 0x00 || frameSubtype == 0x02 || frameSubtype == 0x04;
    }

    if (type == WIFI_PKT_DATA) {
        return toDs && !fromDs;
    }

    return false;
}

void update_proximity_result(const wifi_promiscuous_pkt_t* packet, wifi_promiscuous_pkt_type_t type)
{
    if (!s_proximity_scan_active || packet == nullptr) {
        return;
    }

    if (type != WIFI_PKT_MGMT && type != WIFI_PKT_DATA) {
        return;
    }

    if (packet->rx_ctrl.sig_len < sizeof(WifiMacHeader)) {
        return;
    }

    const auto* header = reinterpret_cast<const WifiMacHeader*>(packet->payload);
    if (header == nullptr) {
        return;
    }

    if (!should_track_client_frame(*header, type)) {
        return;
    }

    const uint8_t* transmitter = header->transmitter;
    if (transmitter[0] == 0 && transmitter[1] == 0 && transmitter[2] == 0 && transmitter[3] == 0 &&
        transmitter[4] == 0 && transmitter[5] == 0) {
        return;
    }

    const auto now     = proximity_now_ms();
    const auto channel = packet->rx_ctrl.channel == 0 ? s_proximity_channel : packet->rx_ctrl.channel;
    const auto rssi    = static_cast<int>(packet->rx_ctrl.rssi);

    auto existing = std::find_if(
        s_proximity_results.begin(), s_proximity_results.end(),
        [transmitter](const auto& item) { return std::memcmp(item.mac.data(), transmitter, item.mac.size()) == 0; });

    if (existing != s_proximity_results.end()) {
        existing->rssi       = rssi;
        existing->channel    = channel;
        existing->lastSeenMs = now;
        existing->hitCount += 1;
        existing->isManagementFrame = (type == WIFI_PKT_MGMT);
        return;
    }

    Hal::ProximityScanResult_t result;
    std::copy_n(transmitter, result.mac.size(), result.mac.begin());
    result.macString         = format_mac_string(transmitter);
    result.rssi              = rssi;
    result.channel           = channel;
    result.firstSeenMs       = now;
    result.lastSeenMs        = now;
    result.hitCount          = 1;
    result.isManagementFrame = (type == WIFI_PKT_MGMT);
    s_proximity_results.push_back(std::move(result));
}

void proximity_promiscuous_rx_cb(void* buf, wifi_promiscuous_pkt_type_t type)
{
    update_proximity_result(reinterpret_cast<const wifi_promiscuous_pkt_t*>(buf), type);
}

void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
    const char* tag = "wifi";

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        xEventGroupSetBits(s_wifi_event_group, WIFI_STARTED_BIT);
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        xEventGroupSetBits(s_wifi_event_group, WIFI_DISCONNECTED_BIT);
        xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = static_cast<ip_event_got_ip_t*>(event_data);
        ESP_LOGI(tag, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}
}  // namespace

void Hal::wifiScan(std::vector<ScanResult_t>& scanResult)
{
    mclog::tagInfo(kHalTag, "wifi scan");

    scanResult.clear();

    uint16_t number   = static_cast<uint16_t>(kDefaultScanListSize);
    uint16_t ap_count = 0;
    std::memset(s_ap_info, 0, sizeof(s_ap_info));

    esp_err_t ret = esp_wifi_scan_start(NULL, true);
    if (ret != ESP_OK) {
        mclog::tagError(kHalTag, "failed to start wifi scan: {}", esp_err_to_name(ret));
        return;
    }

    ret = esp_wifi_scan_get_ap_num(&ap_count);
    if (ret != ESP_OK) {
        mclog::tagError(kHalTag, "failed to get AP number: {}", esp_err_to_name(ret));
        return;
    }

    ret = esp_wifi_scan_get_ap_records(&number, s_ap_info);
    if (ret != ESP_OK) {
        mclog::tagError(kHalTag, "failed to get AP records: {}", esp_err_to_name(ret));
        return;
    }

    for (int i = 0; i < number; i++) {
        std::string ssid = reinterpret_cast<char*>(s_ap_info[i].ssid);
        int rssi         = s_ap_info[i].rssi;

        if (ssid.empty()) {
            continue;
        }

        scanResult.push_back(std::make_pair(rssi, ssid));
    }

    std::sort(
        scanResult.begin(), scanResult.end(),
        [](const std::pair<int, std::string>& a, const std::pair<int, std::string>& b) { return a.first > b.first; });

    mclog::tagInfo(kHalTag, "wifi scan completed, found {} APs", scanResult.size());
}

bool Hal::wifiProximityScanStart()
{
    mclog::tagInfo(kHalTag, "wifi proximity scan start");

    if (s_proximity_scan_active) {
        return true;
    }

    if (!_is_wifi_inited) {
        wifiInit();
    }

    if (_is_wifi_connected) {
        wifiDisconnect();
    }

    if (_is_esp_now_inited) {
        espNowDeinit();
    }

    esp_err_t ret = esp_wifi_stop();
    if (ret != ESP_OK && ret != ESP_ERR_WIFI_NOT_INIT && ret != ESP_ERR_WIFI_NOT_STARTED) {
        mclog::tagError(kHalTag, "failed to stop wifi before proximity scan: {}", esp_err_to_name(ret));
        return false;
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_NULL));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_set_channel(kProximityScanFirstChannel, WIFI_SECOND_CHAN_NONE));
    ESP_ERROR_CHECK(esp_wifi_set_promiscuous_filter(&s_proximity_filter));
    ESP_ERROR_CHECK(esp_wifi_set_promiscuous_rx_cb(&proximity_promiscuous_rx_cb));
    ESP_ERROR_CHECK(esp_wifi_set_promiscuous(true));

    s_proximity_results.clear();
    s_proximity_channel     = kProximityScanFirstChannel;
    s_proximity_next_hop_ms = millis() + kProximityScanHopIntervalMs;
    s_proximity_scan_active = true;
    return true;
}

void Hal::wifiProximityScanStop()
{
    mclog::tagInfo(kHalTag, "wifi proximity scan stop");

    if (!s_proximity_scan_active) {
        return;
    }

    ESP_ERROR_CHECK(esp_wifi_set_promiscuous(false));
    ESP_ERROR_CHECK(esp_wifi_set_promiscuous_rx_cb(nullptr));

    esp_err_t ret = esp_wifi_stop();
    if (ret != ESP_OK && ret != ESP_ERR_WIFI_NOT_INIT && ret != ESP_ERR_WIFI_NOT_STARTED) {
        mclog::tagError(kHalTag, "failed to stop wifi after proximity scan: {}", esp_err_to_name(ret));
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());

    s_proximity_scan_active = false;
    s_proximity_channel     = kProximityScanFirstChannel;
    s_proximity_next_hop_ms = 0;
    s_proximity_results.clear();
}

void Hal::wifiProximityScanPoll()
{
    if (!s_proximity_scan_active) {
        return;
    }

    const auto now = millis();
    if (now >= s_proximity_next_hop_ms) {
        s_proximity_channel++;
        if (s_proximity_channel > kProximityScanLastChannel) {
            s_proximity_channel = kProximityScanFirstChannel;
        }
        ESP_ERROR_CHECK(esp_wifi_set_channel(s_proximity_channel, WIFI_SECOND_CHAN_NONE));
        s_proximity_next_hop_ms = now + kProximityScanHopIntervalMs;
    }

    const auto now_ms = proximity_now_ms();
    const auto cutoff = now_ms > kProximityScanStaleMs ? now_ms - kProximityScanStaleMs : 0;
    s_proximity_results.erase(std::remove_if(s_proximity_results.begin(), s_proximity_results.end(),
                                             [cutoff](const auto& item) { return item.lastSeenMs < cutoff; }),
                              s_proximity_results.end());
}

void Hal::wifiProximityScanGetDevices(std::vector<ProximityScanResult_t>& scanResult)
{
    scanResult = s_proximity_results;
    std::sort(scanResult.begin(), scanResult.end(), [](const auto& left, const auto& right) {
        if (left.rssi != right.rssi) {
            return left.rssi > right.rssi;
        }
        return left.lastSeenMs > right.lastSeenMs;
    });
}

bool Hal::isWifiProximityScanActive() const
{
    return s_proximity_scan_active;
}

void Hal::wifiInit()
{
    mclog::tagInfo(kHalTag, "wifi init");

    if (_is_wifi_inited) {
        return;
    }

    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_t* sta_netif = esp_netif_create_default_wifi_sta();
    assert(sta_netif);

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));

    if (!s_wifi_event_group) {
        s_wifi_event_group = xEventGroupCreate();
    }

    ESP_ERROR_CHECK(
        esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, nullptr, nullptr));
    ESP_ERROR_CHECK(
        esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, nullptr, nullptr));

    ESP_ERROR_CHECK(esp_wifi_start());
    _is_wifi_inited = true;
}

void Hal::wifiDeinit()
{
    mclog::tagInfo(kHalTag, "wifi deinit");

    if (!_is_wifi_inited) {
        return;
    }

    esp_wifi_stop();
    esp_wifi_deinit();
    _is_wifi_inited    = false;
    _is_wifi_connected = false;
}

bool Hal::wifiConnect(const std::string& ssid, const std::string& password)
{
    mclog::tagInfo(kHalTag, "wifi connect to ssid: {} password: {}", ssid, password);

    if (!_is_wifi_inited) {
        wifiInit();
    }

    wifiDisconnect();

    xEventGroupWaitBits(s_wifi_event_group, WIFI_STARTED_BIT, pdFALSE, pdFALSE, pdMS_TO_TICKS(3000));
    xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT | WIFI_DISCONNECTED_BIT);

    wifi_config_t wifi_config = {};
    std::strncpy(reinterpret_cast<char*>(wifi_config.sta.ssid), ssid.c_str(), sizeof(wifi_config.sta.ssid));
    std::strncpy(reinterpret_cast<char*>(wifi_config.sta.password), password.c_str(), sizeof(wifi_config.sta.password));

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_connect());

    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT, pdTRUE, pdFALSE,
                                           pdMS_TO_TICKS(10000));

    if (bits & WIFI_CONNECTED_BIT) {
        mclog::tagInfo(kHalTag, "connected to SSID: {}", ssid);
        _is_wifi_connected = true;
        start_sntp();
        return true;
    }

    if (bits & WIFI_FAIL_BIT) {
        mclog::tagError(kHalTag, "failed to connect to SSID: {}", ssid);
        return false;
    }

    mclog::tagError(kHalTag, "wifi connect timeout");
    return false;
}

void Hal::wifiDisconnect()
{
    mclog::tagInfo(kHalTag, "wifi disconnect");

    if (!_is_wifi_inited || !_is_wifi_connected) {
        return;
    }

    xEventGroupWaitBits(s_wifi_event_group, WIFI_STARTED_BIT, pdFALSE, pdFALSE, pdMS_TO_TICKS(3000));
    ESP_ERROR_CHECK(esp_wifi_disconnect());
    xEventGroupWaitBits(s_wifi_event_group, WIFI_DISCONNECTED_BIT, pdFALSE, pdFALSE, pdMS_TO_TICKS(5000));
    xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT | WIFI_DISCONNECTED_BIT);

    stop_sntp();
    _is_wifi_connected = false;
}

void Hal::start_sntp()
{
    mclog::tagInfo(kHalTag, "start sntp");

    if (!_is_wifi_connected) {
        mclog::tagError(kHalTag, "wifi not connected");
        return;
    }

    setenv("TZ", "CST-8", 1);
    tzset();

    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_init();
}

void Hal::stop_sntp()
{
    mclog::tagInfo(kHalTag, "stop sntp");

    if (!_is_wifi_connected) {
        mclog::tagError(kHalTag, "wifi not connected");
        return;
    }

    esp_sntp_stop();
}