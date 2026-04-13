/*
 * SPDX-FileCopyrightText: 2025 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "hal.h"
#include "hal_config.h"
#include <mooncake_log.h>
#include <driver/sdmmc_host.h>
#include <driver/sdspi_host.h>
#include <driver/spi_master.h>
#include <esp_vfs_fat.h>
#include <sdmmc_cmd.h>

namespace {
constexpr char kHalTag[]      = "HAL";
constexpr char kMountPoint[]  = "/sdcard";
constexpr char kSdProbePath[] = "/sdcard/test.txt";

bool s_spi_bus_initialized = false;
sdmmc_card_t* s_sd_card    = nullptr;
}  // namespace

void Hal::spi_init()
{
    mclog::tagInfo(kHalTag, "spi init");

    sdmmc_host_t host        = SDSPI_HOST_DEFAULT();
    spi_bus_config_t bus_cfg = {};
    bus_cfg.mosi_io_num      = HAL_PIN_SPI_MOSI;
    bus_cfg.miso_io_num      = HAL_PIN_SPI_MISO;
    bus_cfg.sclk_io_num      = HAL_PIN_SPI_SCLK;
    bus_cfg.quadwp_io_num    = -1;
    bus_cfg.quadhd_io_num    = -1;
    bus_cfg.max_transfer_sz  = 4000;

    if (!s_spi_bus_initialized) {
        const esp_err_t ret =
            spi_bus_initialize(static_cast<spi_host_device_t>(host.slot), &bus_cfg, SDSPI_DEFAULT_DMA);
        if (ret != ESP_OK) {
            mclog::tagError(kHalTag, "failed to initialize SPI bus");
            return;
        }
        s_spi_bus_initialized = true;
        mclog::tagInfo(kHalTag, "spi bus initialized");
        return;
    }

    mclog::tagWarn(kHalTag, "spi bus already initialized, reusing");
}

void Hal::sd_card_init()
{
    mclog::tagInfo(kHalTag, "sd card init");

    if (!s_spi_bus_initialized) {
        spi_init();
    }

    if (_is_sd_card_mounted) {
        mclog::tagInfo(kHalTag, "sd card already mounted");
        return;
    }

    sdmmc_host_t host                             = SDSPI_HOST_DEFAULT();
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {};
    mount_config.format_if_mount_failed           = false;
    mount_config.max_files                        = 5;
    mount_config.allocation_unit_size             = 16 * 1024;

    mclog::tagInfo(kHalTag, "initializing SD card");

    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs               = HAL_PIN_SD_CARD_CS;
    slot_config.host_id               = static_cast<spi_host_device_t>(host.slot);

    mclog::tagInfo(kHalTag, "mounting filesystem");
    const esp_err_t ret = esp_vfs_fat_sdspi_mount(kMountPoint, &host, &slot_config, &mount_config, &s_sd_card);

    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            mclog::tagError(kHalTag, "failed to mount filesystem");
        } else {
            mclog::tagError(kHalTag, "failed to initialize the card, make sure SD card lines have pull-up resistors");
        }

        mclog::tagInfo(kHalTag, "sd card init failed, but spi bus remains initialized for retry");
        return;
    }

    mclog::tagInfo(kHalTag, "filesystem mounted successfully");
    sdmmc_card_print_info(stdout, s_sd_card);
    _is_sd_card_mounted = true;
}

Hal::SdCardProbeResult_t Hal::sdCardProbe()
{
    SdCardProbeResult_t result;

    if (!_is_sd_card_mounted) {
        sd_card_init();
        if (!_is_sd_card_mounted) {
            result.is_mounted = false;
            result.size       = "Not Found";
            return result;
        }
    }

    result.is_mounted = true;

    FILE* fp = fopen(kSdProbePath, "w");
    if (fp) {
        fwrite("Hello, World!", 1, 13, fp);
        fclose(fp);

        result.size = fmt::format(
            "Size: {:.1f} GB",
            (static_cast<float>(static_cast<uint64_t>(s_sd_card->csd.capacity) * s_sd_card->csd.sector_size)) /
                (1024 * 1024 * 1024));
    } else {
        result.size = "Write Failed";
    }

    result.type = "Type: ";
    if (s_sd_card->is_sdio) {
        result.type += "SDIO";
    } else if (s_sd_card->is_mmc) {
        result.type += "MMC";
    } else {
        result.type += (s_sd_card->ocr & (1 << 30)) ? "SDHC/SDXC" : "SDSC";
    }

    result.name = fmt::format("Name: {}", std::string(s_sd_card->cid.name));
    return result;
}