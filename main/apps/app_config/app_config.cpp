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
#include <hal.h>
#include <mooncake_log.h>
#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <string>
#include <sys/stat.h>

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

bool has_text_input_modifiers(uint8_t modifierMask)
{
    return (modifierMask &
            (KEY_MOD_LCTRL | KEY_MOD_RCTRL | KEY_MOD_LMETA | KEY_MOD_RMETA | KEY_MOD_LALT | KEY_MOD_RALT)) != 0;
}

bool is_valid_file_name(const std::string& fileName, std::string& errorMessage)
{
    if (fileName.empty()) {
        errorMessage = "file name required";
        return false;
    }

    if (fileName == "." || fileName == "..") {
        errorMessage = "invalid file name";
        return false;
    }

    for (char ch : fileName) {
        if (ch == '/' || ch == '\\') {
            errorMessage = "slashes are not allowed";
            return false;
        }

        if (static_cast<unsigned char>(ch) < 32) {
            errorMessage = "invalid file name";
            return false;
        }
    }

    return true;
}
}  // namespace

AppConfig::AppConfig()
{
    setAppInfo().name     = "Editor";
    setAppInfo().userData = new AppIcon_t(image_data_tf_big, image_data_tf_small, false);
}

AppConfig::~AppConfig()
{
    delete static_cast<AppIcon_t*>(getAppInfo().userData);
}

void AppConfig::onOpen()
{
    mclog::tagInfo(getAppInfo().name, "on open");

    _editor            = std::make_unique<AppConfigEditor>();
    _browser           = std::make_unique<SdFileBrowser>();
    _key_event_slot_id = GetHAL().keyboard.onKeyEvent.connect(
        [this](const Keyboard::KeyEvent_t& keyEvent) { handle_key_event(keyEvent); });
    _cursor_visible     = true;
    _cursor_update_time = GetHAL().millis();
    _state              = ViewState::Browser;
    _active_file_path.clear();
    _create_file_name.clear();

    GetHAL().canvas.setBaseColor(THEME_COLOR_BG);
    GetHAL().canvas.setTextSize(1);
    GetHAL().canvas.setTextScroll(false);

    update_viewport_metrics();
    refresh_file_browser();
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
    _browser.reset();
}

void AppConfig::load_editor_content(const std::string& filePath)
{
    std::string errorMessage;
    if (_editor->loadFromFile(filePath.c_str(), errorMessage)) {
        _active_file_path   = filePath;
        _state              = ViewState::Editor;
        _cursor_visible     = true;
        _cursor_update_time = GetHAL().millis();
        mclog::tagInfo(getAppInfo().name, "loaded {}", filePath);
        return;
    }

    _browser->setStatusMessage(fmt::format("Open failed: {}", errorMessage));
    mclog::tagWarn(getAppInfo().name, "failed to load {}: {}", filePath, errorMessage);
}

void AppConfig::refresh_file_browser()
{
    if (!_browser) {
        return;
    }

    if (!GetHAL().ensureSdCardMounted()) {
        _browser->clear();
        _browser->setStatusMessage("Insert SD card  N disabled");
        return;
    }

    std::string errorMessage;
    if (!_browser->refresh(errorMessage)) {
        _browser->clear();
        _browser->setStatusMessage(fmt::format("Browse failed: {}", errorMessage));
        mclog::tagWarn(getAppInfo().name, "failed to refresh {}: {}", SdFileBrowser::ROOT_PATH, errorMessage);
        return;
    }

    if (_browser->getEntryCount() == 0) {
        _browser->setStatusMessage(fmt::format("N New  No files in {}", _browser->getCurrentPath()));
    } else {
        _browser->setStatusMessage("N New  Enter Open  D Delete");
    }
}

void AppConfig::open_selected_file()
{
    if (_browser == nullptr || !_browser->hasSelection()) {
        return;
    }

    const auto* entry = _browser->getSelectedEntry();
    if (entry == nullptr) {
        return;
    }

    if (entry->is_directory) {
        std::string errorMessage;
        if (!_browser->enterSelectedDirectory(errorMessage)) {
            _browser->setStatusMessage(fmt::format("Browse failed: {}", errorMessage));
            return;
        }

        if (_browser->getEntryCount() == 0) {
            _browser->setStatusMessage(fmt::format("N New  No files in {}", _browser->getCurrentPath()));
        } else {
            _browser->setStatusMessage("N New  Enter Open  D Delete");
        }
        return;
    }

    load_editor_content(entry->path);
}

void AppConfig::delete_selected_file()
{
    if (_browser == nullptr) {
        return;
    }

    const auto* entry = _browser->getSelectedEntry();
    if (entry == nullptr) {
        _browser->setStatusMessage("Delete failed: no file selected");
        return;
    }

    if (entry->is_directory) {
        _browser->setStatusMessage("Delete failed: directories are not supported");
        return;
    }

    const std::string deletedPath = entry->path;
    const std::string deletedName = entry->name;
    if (std::remove(deletedPath.c_str()) == 0) {
        mclog::tagInfo(getAppInfo().name, "deleted {}", deletedPath);
        refresh_file_browser();
        if (_browser != nullptr) {
            _browser->setStatusMessage(fmt::format("Deleted {}", deletedName));
        }
        return;
    }

    const std::string errorMessage = std::strerror(errno);
    _browser->setStatusMessage(fmt::format("Delete failed: {}", errorMessage));
    mclog::tagWarn(getAppInfo().name, "failed to delete {}: {}", deletedPath, errorMessage);
}

bool AppConfig::create_file(const std::string& fileName, std::string& createdPath, std::string& errorMessage)
{
    if (!is_valid_file_name(fileName, errorMessage)) {
        return false;
    }

    const std::string currentPath = _browser ? _browser->getCurrentPath() : std::string(SdFileBrowser::ROOT_PATH);
    const std::string path        = SdFileBrowser::buildPath(currentPath, fileName);
    struct stat fileInfo          = {};
    if (stat(path.c_str(), &fileInfo) == 0) {
        errorMessage = "file already exists";
        return false;
    }

    if (errno != ENOENT) {
        errorMessage = std::strerror(errno);
        return false;
    }

    FILE* file = fopen(path.c_str(), "wb");
    if (file == nullptr) {
        errorMessage = std::strerror(errno);
        return false;
    }

    if (fclose(file) != 0) {
        errorMessage = std::strerror(errno);
        return false;
    }

    createdPath = path;
    return true;
}

void AppConfig::abort_editor()
{
    const std::string abortedPath = _active_file_path;

    _editor = std::make_unique<AppConfigEditor>();
    _active_file_path.clear();
    _state              = ViewState::Browser;
    _cursor_visible     = true;
    _cursor_update_time = GetHAL().millis();
    update_viewport_metrics();

    if (_browser != nullptr) {
        _browser->setStatusMessage(abortedPath.empty() ? "Edit aborted" : fmt::format("Aborted {}", abortedPath));
    }

    mclog::tagInfo(getAppInfo().name, "aborted edit for {}", abortedPath.empty() ? "(no file)" : abortedPath);
}

void AppConfig::begin_create_file()
{
    if (!GetHAL().ensureSdCardMounted()) {
        _browser->setStatusMessage("Insert SD card before creating files");
        return;
    }

    _create_file_name.clear();
    _state = ViewState::CreateFile;
}

void AppConfig::cancel_create_file()
{
    _create_file_name.clear();
    _state = ViewState::Browser;
    if (_browser) {
        if (_browser->getEntryCount() == 0) {
            _browser->setStatusMessage(fmt::format("N New  No files in {}", _browser->getCurrentPath()));
        } else {
            _browser->setStatusMessage("N New  Enter Open  D Delete");
        }
    }
}

void AppConfig::commit_create_file()
{
    if (_browser == nullptr) {
        return;
    }

    std::string createdPath;
    std::string errorMessage;
    if (!create_file(_create_file_name, createdPath, errorMessage)) {
        _browser->setStatusMessage(fmt::format("Create failed: {}", errorMessage));
        return;
    }

    mclog::tagInfo(getAppInfo().name, "created {}", createdPath);
    refresh_file_browser();
    if (_browser != nullptr) {
        _browser->setStatusMessage(fmt::format("Created {}", _create_file_name));
    }
    _create_file_name.clear();
    _state = ViewState::Browser;
}

void AppConfig::update_viewport_metrics()
{
    const std::size_t viewportRows =
        std::max<int>(1, (GetHAL().canvas.height() - STATUS_BAR_HEIGHT) / FONT_REPL_HEIGHT);
    const std::size_t viewportColumns = std::max<int>(1, GetHAL().canvas.width() / FONT_REPL_WIDTH);

    if (_editor) {
        _editor->setViewportSize(viewportRows, viewportColumns);
    }

    if (_browser) {
        _browser->setViewportRows(viewportRows);
    }
}

void AppConfig::render()
{
    GetHAL().canvas.fillScreen(THEME_COLOR_BG);
    render_status_bar();
    if (_state == ViewState::Editor) {
        render_document();
        render_cursor();
    } else {
        render_browser();
    }
    GetHAL().pushCanvas();
}

void AppConfig::render_status_bar()
{
    std::string header;
    std::string status;

    if (_state == ViewState::Editor) {
        header = fmt::format("{}{} {}", _editor->isEditable() ? "EDIT" : "READ", _editor->isDirty() ? "*" : " ",
                             _active_file_path.empty() ? "(no file)" : _active_file_path);
        status = _editor->getStatusMessage();
    } else if (_state == ViewState::CreateFile) {
        header =
            fmt::format("BROWSE {}", _browser ? _browser->getCurrentPath() : std::string(SdFileBrowser::ROOT_PATH));
        status = fmt::format("NEW: {}_  Enter Create  Esc Cancel", _create_file_name);
    } else {
        header =
            fmt::format("BROWSE {}", _browser ? _browser->getCurrentPath() : std::string(SdFileBrowser::ROOT_PATH));
        status = _browser ? _browser->getStatusMessage() : std::string();
    }

    GetHAL().canvas.setFont(FONT_SMALL);
    GetHAL().canvas.setTextColor(TFT_ORANGE, THEME_COLOR_BG);
    GetHAL().canvas.drawString(truncate_status_text(header, 36).c_str(), 0, 0);
    GetHAL().canvas.setTextColor(TFT_CYAN, THEME_COLOR_BG);
    GetHAL().canvas.drawString(truncate_status_text(status, 36).c_str(), 0, 9);
}

void AppConfig::render_browser()
{
    GetHAL().canvas.setFont(FONT_REPL);

    if (_browser == nullptr || _browser->getEntryCount() == 0) {
        GetHAL().canvas.setTextColor(TFT_WHITE, THEME_COLOR_BG);
        GetHAL().canvas.drawString("(no files)", 0, STATUS_BAR_HEIGHT);
        return;
    }

    const std::size_t firstIndex = _browser->getFirstVisibleIndex();
    for (std::size_t row = 0; row < _browser->getViewportRows(); ++row) {
        const std::size_t entryIndex = firstIndex + row;
        const auto* entry            = _browser->getEntry(entryIndex);
        if (entry == nullptr) {
            break;
        }

        const bool isSelected = entryIndex == _browser->getSelectedIndex();
        const int y           = STATUS_BAR_HEIGHT + static_cast<int>(row * FONT_REPL_HEIGHT);
        if (isSelected) {
            GetHAL().canvas.fillRect(0, y, GetHAL().canvas.width(), FONT_REPL_HEIGHT, TFT_DARKGREEN);
            GetHAL().canvas.setTextColor(TFT_WHITE, TFT_DARKGREEN);
        } else {
            GetHAL().canvas.setTextColor(TFT_WHITE, THEME_COLOR_BG);
        }

        std::string label = entry->name;
        if (entry->is_parent) {
            label = "../";
        } else if (entry->is_directory) {
            label += "/";
        }

        GetHAL().canvas.drawString(truncate_status_text(label, _editor->getViewportColumns()).c_str(), 0, y);
    }
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
    if (_state == ViewState::Editor) {
        handle_editor_key_event(keyEvent, modifierMask);
    } else if (_state == ViewState::CreateFile) {
        handle_create_file_key_event(keyEvent, modifierMask);
    } else {
        handle_browser_key_event(keyEvent, modifierMask);
    }
}

void AppConfig::handle_browser_key_event(const Keyboard::KeyEvent_t& keyEvent, uint8_t modifierMask)
{
    bool shouldRender = false;

    if (keyEvent.keyCode == KEY_SEMICOLON || keyEvent.keyCode == KEY_UP) {
        shouldRender = _browser->moveUp();
    } else if (keyEvent.keyCode == KEY_DOT || keyEvent.keyCode == KEY_DOWN) {
        shouldRender = _browser->moveDown();
    } else if (keyEvent.keyCode == KEY_ENTER) {
        open_selected_file();
        shouldRender = true;
    } else if (keyEvent.keyCode == KEY_N && !has_text_input_modifiers(modifierMask)) {
        begin_create_file();
        shouldRender = true;
    } else if (keyEvent.keyCode == KEY_D && !has_text_input_modifiers(modifierMask)) {
        delete_selected_file();
        shouldRender = true;
    }

    if (shouldRender) {
        render();
    }
}

void AppConfig::handle_create_file_key_event(const Keyboard::KeyEvent_t& keyEvent, uint8_t modifierMask)
{
    bool shouldRender = false;

    if (keyEvent.keyCode == KEY_ENTER) {
        commit_create_file();
        shouldRender = true;
    } else if (keyEvent.keyCode == KEY_ESC) {
        cancel_create_file();
        shouldRender = true;
    } else if (keyEvent.keyCode == KEY_BACKSPACE) {
        if (!_create_file_name.empty()) {
            _create_file_name.pop_back();
            shouldRender = true;
        }
    } else if (keyEvent.keyCode == KEY_SPACE && !has_text_input_modifiers(modifierMask)) {
        _create_file_name.push_back(' ');
        shouldRender = true;
    } else if (keyEvent.keyName != nullptr && strlen(keyEvent.keyName) == 1 &&
               !has_text_input_modifiers(modifierMask)) {
        _create_file_name.push_back(keyEvent.keyName[0]);
        shouldRender = true;
    }

    if (shouldRender) {
        render();
    }
}

void AppConfig::handle_editor_key_event(const Keyboard::KeyEvent_t& keyEvent, uint8_t modifierMask)
{
    bool shouldRender = false;

    if ((modifierMask & KEY_MOD_LMETA) != 0 && keyEvent.keyCode == KEY_S) {
        save_file();
        render();
        return;
    }
    if ((modifierMask & KEY_MOD_LMETA) != 0 && keyEvent.keyCode == KEY_GRAVE) {
        abort_editor();
        render();
        return;
    }

    if ((modifierMask & KEY_MOD_LMETA) != 0 && keyEvent.keyCode == KEY_B) {
        shouldRender = _editor->moveLineStart();
    } else if ((modifierMask & KEY_MOD_LMETA) != 0 && keyEvent.keyCode == KEY_E) {
        shouldRender = _editor->moveLineEnd();
    } else if (((modifierMask & KEY_MOD_LCTRL) != 0 && keyEvent.keyCode == KEY_COMMA) ||
               ((modifierMask & KEY_MOD_LCTRL) != 0 && keyEvent.keyCode == KEY_H)) {
        shouldRender = _editor->moveLeft();
    } else if (((modifierMask & KEY_MOD_LCTRL) != 0 && keyEvent.keyCode == KEY_SLASH) ||
               ((modifierMask & KEY_MOD_LCTRL) != 0 && keyEvent.keyCode == KEY_L)) {
        shouldRender = _editor->moveRight();
    } else if (((modifierMask & KEY_MOD_LCTRL) != 0 && keyEvent.keyCode == KEY_SEMICOLON) ||
               ((modifierMask & KEY_MOD_LCTRL) != 0 && keyEvent.keyCode == KEY_K)) {
        shouldRender = _editor->moveUp();
    } else if (((modifierMask & KEY_MOD_LCTRL) != 0 && keyEvent.keyCode == KEY_DOT) ||
               ((modifierMask & KEY_MOD_LCTRL) != 0 && keyEvent.keyCode == KEY_J)) {
        shouldRender = _editor->moveDown();
    } else if (keyEvent.keyCode == KEY_BACKSPACE) {
        shouldRender = _editor->backspace();
    } else if (keyEvent.keyCode == KEY_ENTER) {
        shouldRender = _editor->insertNewline();
    } else if (keyEvent.keyCode == KEY_TAB) {
        shouldRender = _editor->insertText("    ");
    } else if (keyEvent.keyCode == KEY_SPACE) {
        shouldRender = _editor->insertChar(' ');
    } else if (keyEvent.keyName != nullptr && strlen(keyEvent.keyName) == 1 &&
               !has_text_input_modifiers(modifierMask)) {
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
    if (!_editor || _state != ViewState::Editor || !_editor->isEditable()) {
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
    if (_active_file_path.empty()) {
        return;
    }

    std::string errorMessage;
    if (_editor->saveToFile(_active_file_path.c_str(), errorMessage)) {
        mclog::tagInfo(getAppInfo().name, "saved {}", _active_file_path);
    } else {
        _editor->setStatusMessage(fmt::format("Save failed: {}", errorMessage));
        mclog::tagWarn(getAppInfo().name, "failed to save {}: {}", _active_file_path, errorMessage);
    }

    _cursor_visible     = true;
    _cursor_update_time = GetHAL().millis();
}