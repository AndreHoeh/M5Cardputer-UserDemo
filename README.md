# Cardputer ADV User Demo

User demo source code of [Cardputer ADV](https://docs.m5stack.com/en/products/sku/K132-Adv).

## Build

### Fetch Dependencies

```bash
python3 ./fetch_repos.py
```

### Tool Chains

[ESP-IDF v5.4.2](https://docs.espressif.com/projects/esp-idf/en/v5.4.2/esp32s3/index.html)

### Build

```bash
idf.py build
```

### Flash

```bash
idf.py flash
```

## SD Settings Config

System settings can be overridden at boot from `/sdcard/settings.conf`.

The config is loaded during launcher startup, after the boot animation path has finished, instead of during early HAL initialization.

Format rules:

- One `key=value` pair per line.
- Empty lines are ignored.
- Lines starting with `#` are treated as comments.
- Inline comments after a value are also allowed with `#`.
- Unknown keys and invalid values are ignored with a warning log.

Supported keys:

- `config_version=1`
- `speaker_volume=0..255`
- `display_brightness=0..255`
- `idle_sleep_timeout_ms=0..3600000`

Example:

```conf
# /sdcard/settings.conf
config_version=1
speaker_volume=30
display_brightness=200
idle_sleep_timeout_ms=60000
```

## Acknowledgments

This project references the following open-source libraries and resources:

- https://github.com/adafruit/Adafruit_TCA8418
- https://github.com/m5stack/M5Unified.git
- https://github.com/pikasTech/PikaPython
- https://github.com/jgromes/RadioLib
- https://github.com/raysan5/raylib
- https://github.com/mikalhart/TinyGPSPlus
- https://github.com/m5stack/M5GFX.git
- https://github.com/Forairaaaaa/mooncake_log
- https://github.com/hhuysqt/esp32s3-keyboard
- https://github.com/78/xiaozhi-esp32
- https://github.com/Forairaaaaa/mooncake
- https://github.com/Forairaaaaa/smooth_ui_toolkit
