/*
 * SPDX-FileCopyrightText: 2025 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "app_config_editor.h"
#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>

namespace {
constexpr const char* STATUS_CONTROLS = "Ctrl+HJKL Move  Opt+S Save";
}

AppConfigEditor::AppConfigEditor()
{
    setBuffer("");
    _status_message = STATUS_CONTROLS;
}

bool AppConfigEditor::loadFromFile(const char* filePath, std::string& errorMessage)
{
    FILE* file = fopen(filePath, "rb");
    if (file == nullptr) {
        errorMessage = std::strerror(errno);
        return false;
    }

    std::string buffer;
    buffer.reserve(256);
    char chunk[128];
    while (true) {
        const std::size_t bytesRead = fread(chunk, 1, sizeof(chunk), file);
        if (bytesRead > 0) {
            if (buffer.size() + bytesRead > MAX_FILE_SIZE) {
                errorMessage = "file exceeds 4096 bytes";
                fclose(file);
                return false;
            }

            buffer.append(chunk, bytesRead);
        }

        if (bytesRead < sizeof(chunk)) {
            if (ferror(file) != 0) {
                errorMessage = std::strerror(errno);
                fclose(file);
                return false;
            }
            break;
        }
    }

    fclose(file);

    setBuffer(std::move(buffer));
    _cursor_index         = 0;
    _editable             = true;
    _dirty                = false;
    _has_loaded_file      = true;
    _status_message       = STATUS_CONTROLS;
    _first_visible_line   = 0;
    _first_visible_column = 0;
    syncPreferredColumn();
    ensureCursorVisible();
    return true;
}

bool AppConfigEditor::saveToFile(const char* filePath, std::string& errorMessage)
{
    if (!_editable || !_has_loaded_file) {
        errorMessage = "read-only";
        return false;
    }

    FILE* file = fopen(filePath, "wb");
    if (file == nullptr) {
        errorMessage = std::strerror(errno);
        return false;
    }

    if (!_buffer.empty()) {
        const std::size_t bytesWritten = fwrite(_buffer.data(), 1, _buffer.size(), file);
        if (bytesWritten != _buffer.size()) {
            errorMessage = std::strerror(errno);
            fclose(file);
            return false;
        }
    }

    if (fflush(file) != 0) {
        errorMessage = std::strerror(errno);
        fclose(file);
        return false;
    }

    if (fclose(file) != 0) {
        errorMessage = std::strerror(errno);
        return false;
    }

    _dirty          = false;
    _status_message = "Saved";
    return true;
}

void AppConfigEditor::showReadOnlyMessage(const std::string& message)
{
    setBuffer(message);
    _cursor_index         = 0;
    _preferred_column     = 0;
    _first_visible_line   = 0;
    _first_visible_column = 0;
    _editable             = false;
    _dirty                = false;
    _has_loaded_file      = false;
    _status_message       = "Read-only";
}

void AppConfigEditor::setViewportSize(std::size_t rows, std::size_t columns)
{
    _viewport_rows    = std::max<std::size_t>(rows, 1);
    _viewport_columns = std::max<std::size_t>(columns, 1);
    ensureCursorVisible();
}

bool AppConfigEditor::insertChar(char ch)
{
    return insertText(std::string(1, ch));
}

bool AppConfigEditor::insertText(const std::string& text)
{
    if (!_editable || text.empty()) {
        return false;
    }

    if (_buffer.size() + text.size() > MAX_FILE_SIZE) {
        _status_message = "File max size reached";
        return false;
    }

    _buffer.insert(_cursor_index, text);
    _cursor_index += text.size();
    _dirty = true;
    rebuildLineStarts();
    syncPreferredColumn();
    ensureCursorVisible();
    return true;
}

bool AppConfigEditor::insertNewline()
{
    return insertChar('\n');
}

bool AppConfigEditor::backspace()
{
    if (!_editable || _cursor_index == 0) {
        return false;
    }

    _buffer.erase(_cursor_index - 1, 1);
    --_cursor_index;
    _dirty = true;
    rebuildLineStarts();
    syncPreferredColumn();
    ensureCursorVisible();
    return true;
}

bool AppConfigEditor::moveLeft()
{
    if (_cursor_index == 0) {
        return false;
    }

    --_cursor_index;
    syncPreferredColumn();
    ensureCursorVisible();
    return true;
}

bool AppConfigEditor::moveRight()
{
    if (_cursor_index >= _buffer.size()) {
        return false;
    }

    ++_cursor_index;
    syncPreferredColumn();
    ensureCursorVisible();
    return true;
}

bool AppConfigEditor::moveUp()
{
    const std::size_t lineIndex = getCursorLine();
    if (lineIndex == 0) {
        return false;
    }

    _cursor_index = getIndexForLineColumn(lineIndex - 1, _preferred_column);
    ensureCursorVisible();
    return true;
}

bool AppConfigEditor::moveDown()
{
    const std::size_t lineIndex = getCursorLine();
    if (lineIndex + 1 >= getLineCount()) {
        return false;
    }

    _cursor_index = getIndexForLineColumn(lineIndex + 1, _preferred_column);
    ensureCursorVisible();
    return true;
}

std::size_t AppConfigEditor::getCursorLine() const
{
    return getLineForIndex(_cursor_index);
}

std::size_t AppConfigEditor::getCursorColumn() const
{
    return _cursor_index - getLineStart(getCursorLine());
}

std::size_t AppConfigEditor::getCursorScreenLine() const
{
    return getCursorLine() - _first_visible_line;
}

std::size_t AppConfigEditor::getCursorScreenColumn() const
{
    return getCursorColumn() - _first_visible_column;
}

bool AppConfigEditor::isCursorVisibleInViewport() const
{
    const std::size_t cursorLine   = getCursorLine();
    const std::size_t cursorColumn = getCursorColumn();
    return cursorLine >= _first_visible_line && cursorLine < _first_visible_line + _viewport_rows &&
           cursorColumn >= _first_visible_column && cursorColumn <= _first_visible_column + _viewport_columns;
}

std::string AppConfigEditor::getVisibleLineText(std::size_t lineIndex) const
{
    if (lineIndex >= getLineCount()) {
        return {};
    }

    const std::size_t lineStart = getLineStart(lineIndex);
    const std::size_t lineEnd   = getLineEnd(lineIndex);
    if (_first_visible_column >= lineEnd - lineStart) {
        return {};
    }

    const std::size_t visibleStart = lineStart + _first_visible_column;
    const std::size_t visibleCount = std::min(_viewport_columns, lineEnd - visibleStart);
    return _buffer.substr(visibleStart, visibleCount);
}

void AppConfigEditor::setBuffer(std::string buffer)
{
    _buffer = std::move(buffer);
    rebuildLineStarts();
}

void AppConfigEditor::rebuildLineStarts()
{
    _line_starts.clear();
    _line_starts.push_back(0);

    for (std::size_t index = 0; index < _buffer.size(); ++index) {
        if (_buffer[index] == '\n') {
            _line_starts.push_back(index + 1);
        }
    }

    if (_cursor_index > _buffer.size()) {
        _cursor_index = _buffer.size();
    }
}

void AppConfigEditor::syncPreferredColumn()
{
    _preferred_column = getCursorColumn();
}

void AppConfigEditor::ensureCursorVisible()
{
    const std::size_t cursorLine   = getCursorLine();
    const std::size_t cursorColumn = getCursorColumn();

    if (cursorLine < _first_visible_line) {
        _first_visible_line = cursorLine;
    } else if (cursorLine >= _first_visible_line + _viewport_rows) {
        _first_visible_line = cursorLine - _viewport_rows + 1;
    }

    if (cursorColumn < _first_visible_column) {
        _first_visible_column = cursorColumn;
    } else if (cursorColumn >= _first_visible_column + _viewport_columns) {
        _first_visible_column = cursorColumn - _viewport_columns + 1;
    }
}

std::size_t AppConfigEditor::getLineForIndex(std::size_t index) const
{
    const auto it = std::upper_bound(_line_starts.begin(), _line_starts.end(), index);
    if (it == _line_starts.begin()) {
        return 0;
    }

    return static_cast<std::size_t>(std::distance(_line_starts.begin(), it) - 1);
}

std::size_t AppConfigEditor::getLineStart(std::size_t lineIndex) const
{
    if (lineIndex >= _line_starts.size()) {
        return _buffer.size();
    }

    return _line_starts[lineIndex];
}

std::size_t AppConfigEditor::getLineEnd(std::size_t lineIndex) const
{
    if (lineIndex + 1 >= _line_starts.size()) {
        return _buffer.size();
    }

    const std::size_t nextLineStart = _line_starts[lineIndex + 1];
    if (nextLineStart > 0 && _buffer[nextLineStart - 1] == '\n') {
        return nextLineStart - 1;
    }

    return nextLineStart;
}

std::size_t AppConfigEditor::getIndexForLineColumn(std::size_t lineIndex, std::size_t column) const
{
    const std::size_t lineStart = getLineStart(lineIndex);
    const std::size_t lineEnd   = getLineEnd(lineIndex);
    return lineStart + std::min(column, lineEnd - lineStart);
}