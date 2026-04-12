/*
 * SPDX-FileCopyrightText: 2025 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "app_config.h"
#include "app_sdcard/assets/tf_big.h"
#include "app_sdcard/assets/tf_small.h"
#include <apps/utils/audio/audio.h>
#include <apps/utils/common.h>
#include <apps/utils/theme.h>
#include <assets.h>
#include <dirent.h>
#include <hal.h>
#include <mooncake_log.h>
#include <cerrno>
#include <cstring>
#include <string>

using namespace mooncake;

namespace {
std::string truncate_status_text(const std::string& value, std::size_t maxLength)
{
    if (value.size() <= maxLength) {
        return value;
    }

    if (maxLength <= 3) {
        return value.substr(0, maxLength);
    }

    return value.substr(0, maxLength - 3) + "...";
}

void log_sd_root_entries(const std::string& logTag)
{
    DIR* directory = opendir("/sdcard");
    if (directory == nullptr) {
        mclog::tagWarn(logTag, "failed to open /sdcard for listing: {}", std::strerror(errno));
        return;
    }

    mclog::tagInfo(logTag, "listing /sdcard root entries");
    dirent* entry = nullptr;
    while ((entry = readdir(directory)) != nullptr) {
        mclog::tagInfo(logTag, "- {}", entry->d_name);
    }

    closedir(directory);
}
}  // namespace

AppConfig::AppConfig()
{
    setAppInfo().name     = "Config";
    setAppInfo().userData = new AppIcon_t(image_data_tf_big, image_data_tf_small);
}

AppConfig::~AppConfig()
{
    delete static_cast<AppIcon_t*>(getAppInfo().userData);
}

void AppConfig::onOpen()
{
    mclog::tagInfo(getAppInfo().name, "on open");

    _editor            = std::make_unique<AppConfigEditor>();
    _key_event_slot_id = GetHAL().keyboard.onKeyEvent.connect(
        [this](const Keyboard::KeyEvent_t& keyEvent) { handle_key_event(keyEvent); });
    _cursor_visible     = true;
    _cursor_update_time = GetHAL().millis();

    GetHAL().canvas.setBaseColor(THEME_COLOR_BG);
    GetHAL().canvas.setTextSize(1);
    GetHAL().canvas.setTextScroll(false);

    update_viewport_metrics();
    load_editor_content();
    render();
}

void AppConfig::onRunning()
{
    update_cursor();

    if (is_app_exit_requested()) {
        audio::play_random_tone();
        close();
    }
}

void AppConfig::onClose()
{
    mclog::tagInfo(getAppInfo().name, "on close");

    if (_key_event_slot_id >= 0) {
        GetHAL().keyboard.onKeyEvent.disconnect(_key_event_slot_id);
        _key_event_slot_id = -1;
    }

    _editor.reset();
}

void AppConfig::load_editor_content()
{
    std::string errorMessage;
    const auto probeResult = GetHAL().sdCardProbe();
    if (!probeResult.is_mounted) {
        _editor->showReadOnlyMessage(
            "/sdcard is not mounted.\n\nInsert an SD card with settings.conf to edit device settings.");
        mclog::tagWarn(getAppInfo().name, "skip config editor load: SD card not mounted");
        return;
    }

    if (_editor->loadFromFile(SETTINGS_CONFIG_PATH, errorMessage)) {
        mclog::tagInfo(getAppInfo().name, "loaded {}", SETTINGS_CONFIG_PATH);
        return;
    }

    log_sd_root_entries(getAppInfo().name);
    _editor->showReadOnlyMessage(fmt::format("{}\n\nOpen failed: {}", SETTINGS_CONFIG_PATH, errorMessage));
    mclog::tagWarn(getAppInfo().name, "failed to load {}: {}", SETTINGS_CONFIG_PATH, errorMessage);
}

void AppConfig::update_viewport_metrics()
{
    const std::size_t viewportRows =
        std::max<int>(1, (GetHAL().canvas.height() - STATUS_BAR_HEIGHT) / FONT_REPL_HEIGHT);
    const std::size_t viewportColumns = std::max<int>(1, GetHAL().canvas.width() / FONT_REPL_WIDTH);

    if (_editor) {
        _editor->setViewportSize(viewportRows, viewportColumns);
    }
}

void AppConfig::render()
{
    GetHAL().canvas.fillScreen(THEME_COLOR_BG);
    render_status_bar();
    render_document();
    render_cursor();
    GetHAL().pushCanvas();
}

void AppConfig::render_status_bar()
{
    const std::string header = fmt::format("{}{} {}", _editor->isEditable() ? "EDIT" : "READ",
                                           _editor->isDirty() ? "*" : " ", SETTINGS_CONFIG_PATH);
    const std::string status = truncate_status_text(_editor->getStatusMessage(), 36);

    GetHAL().canvas.setFont(FONT_SMALL);
    GetHAL().canvas.setTextColor(TFT_ORANGE, THEME_COLOR_BG);
    GetHAL().canvas.drawString(truncate_status_text(header, 36).c_str(), 0, 0);
    GetHAL().canvas.setTextColor(TFT_CYAN, THEME_COLOR_BG);
    GetHAL().canvas.drawString(status.c_str(), 0, 9);
}

void AppConfig::render_document()
{
    GetHAL().canvas.setFont(FONT_REPL);
    GetHAL().canvas.setTextColor(TFT_WHITE, THEME_COLOR_BG);

    const std::size_t firstLine = _editor->getFirstVisibleLine();
    for (std::size_t row = 0; row < _editor->getViewportRows(); ++row) {
        const std::size_t lineIndex = firstLine + row;
        const std::string lineText  = _editor->getVisibleLineText(lineIndex);
        GetHAL().canvas.drawString(lineText.c_str(), 0, STATUS_BAR_HEIGHT + static_cast<int>(row * FONT_REPL_HEIGHT));
    }
}

void AppConfig::render_cursor()
{
    if (!_editor->isEditable() || !_cursor_visible || !_editor->isCursorVisibleInViewport()) {
        return;
    }

    const int cursorX = static_cast<int>(_editor->getCursorScreenColumn() * FONT_REPL_WIDTH);
    const int cursorY = STATUS_BAR_HEIGHT + static_cast<int>(_editor->getCursorScreenLine() * FONT_REPL_HEIGHT);
    GetHAL().canvas.fillRect(cursorX, cursorY + 2, 2, FONT_REPL_HEIGHT - 4, TFT_YELLOW);
}

void AppConfig::handle_key_event(const Keyboard::KeyEvent_t& keyEvent)
{
    if (!_editor || !keyEvent.state || keyEvent.isModifier) {
        return;
    }

    const uint8_t modifierMask = GetHAL().keyboard.getModifierMask();
    bool shouldRender          = false;

    if ((modifierMask & KEY_MOD_LMETA) != 0 && keyEvent.keyCode == KEY_S) {
        save_file();
        return;
    }

    if (keyEvent.keyCode == KEY_LEFT || ((modifierMask & KEY_MOD_LCTRL) != 0 && keyEvent.keyCode == KEY_H)) {
        shouldRender = _editor->moveLeft();
    } else if (keyEvent.keyCode == KEY_RIGHT || ((modifierMask & KEY_MOD_LCTRL) != 0 && keyEvent.keyCode == KEY_L)) {
        shouldRender = _editor->moveRight();
    } else if (keyEvent.keyCode == KEY_UP || ((modifierMask & KEY_MOD_LCTRL) != 0 && keyEvent.keyCode == KEY_K)) {
        shouldRender = _editor->moveUp();
    } else if (keyEvent.keyCode == KEY_DOWN || ((modifierMask & KEY_MOD_LCTRL) != 0 && keyEvent.keyCode == KEY_J)) {
        shouldRender = _editor->moveDown();
    } else if (keyEvent.keyCode == KEY_BACKSPACE) {
        shouldRender = _editor->backspace();
    } else if (keyEvent.keyCode == KEY_ENTER) {
        shouldRender = _editor->insertNewline();
    } else if (keyEvent.keyCode == KEY_TAB) {
        shouldRender = _editor->insertText("    ");
    } else if (keyEvent.keyCode == KEY_SPACE) {
        shouldRender = _editor->insertChar(' ');
    } else if (keyEvent.keyName != nullptr && strlen(keyEvent.keyName) == 1 && modifierMask == 0) {
        shouldRender = _editor->insertChar(keyEvent.keyName[0]);
    }

    if (shouldRender) {
        _cursor_visible     = true;
        _cursor_update_time = GetHAL().millis();
        render();
    }
}

void AppConfig::update_cursor()
{
    if (!_editor || !_editor->isEditable()) {
        return;
    }

    if (GetHAL().millis() - _cursor_update_time > CURSOR_BLINK_PERIOD_MS) {
        _cursor_visible     = !_cursor_visible;
        _cursor_update_time = GetHAL().millis();
        render();
    }
}

void AppConfig::save_file()
{
    std::string errorMessage;
    if (_editor->saveToFile(SETTINGS_CONFIG_PATH, errorMessage)) {
        mclog::tagInfo(getAppInfo().name, "saved {}", SETTINGS_CONFIG_PATH);
    } else {
        _editor->setStatusMessage(fmt::format("Save failed: {}", errorMessage));
        mclog::tagWarn(getAppInfo().name, "failed to save {}: {}", SETTINGS_CONFIG_PATH, errorMessage);
    }

    _cursor_visible     = true;
    _cursor_update_time = GetHAL().millis();
    render();
}