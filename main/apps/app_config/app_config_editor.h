/*
 * SPDX-FileCopyrightText: 2025 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <cstddef>
#include <string>
#include <vector>

class AppConfigEditor {
public:
    static constexpr std::size_t MAX_FILE_SIZE = 4096;

    AppConfigEditor();

    bool loadFromFile(const char* filePath, std::string& errorMessage);
    bool saveToFile(const char* filePath, std::string& errorMessage);
    void showReadOnlyMessage(const std::string& message);

    void setViewportSize(std::size_t rows, std::size_t columns);

    bool insertChar(char ch);
    bool insertText(const std::string& text);
    bool insertNewline();
    bool backspace();

    bool moveLeft();
    bool moveRight();
    bool moveUp();
    bool moveDown();
    bool moveLineStart();
    bool moveLineEnd();

    bool isEditable() const
    {
        return _editable;
    }

    bool isDirty() const
    {
        return _dirty;
    }

    bool hasLoadedFile() const
    {
        return _has_loaded_file;
    }

    const std::string& getStatusMessage() const
    {
        return _status_message;
    }

    void setStatusMessage(const std::string& message)
    {
        _status_message = message;
    }

    std::size_t getLineCount() const
    {
        return _line_starts.size();
    }

    std::size_t getFirstVisibleLine() const
    {
        return _first_visible_line;
    }

    std::size_t getFirstVisibleColumn() const
    {
        return _first_visible_column;
    }

    std::size_t getViewportRows() const
    {
        return _viewport_rows;
    }

    std::size_t getViewportColumns() const
    {
        return _viewport_columns;
    }

    std::size_t getCursorLine() const;
    std::size_t getCursorColumn() const;
    std::size_t getCursorScreenLine() const;
    std::size_t getCursorScreenColumn() const;
    bool isCursorVisibleInViewport() const;
    std::string getVisibleLineText(std::size_t lineIndex) const;

private:
    std::string _buffer;
    std::vector<std::size_t> _line_starts;
    std::size_t _cursor_index         = 0;
    std::size_t _preferred_column     = 0;
    std::size_t _first_visible_line   = 0;
    std::size_t _first_visible_column = 0;
    std::size_t _viewport_rows        = 1;
    std::size_t _viewport_columns     = 1;
    bool _editable                    = false;
    bool _dirty                       = false;
    bool _has_loaded_file             = false;
    std::string _status_message;

    void setBuffer(std::string buffer);
    void rebuildLineStarts();
    void syncPreferredColumn();
    void ensureCursorVisible();
    std::size_t getLineForIndex(std::size_t index) const;
    std::size_t getLineStart(std::size_t lineIndex) const;
    std::size_t getLineEnd(std::size_t lineIndex) const;
    std::size_t getIndexForLineColumn(std::size_t lineIndex, std::size_t column) const;
};