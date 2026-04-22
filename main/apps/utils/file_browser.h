/*
 * SPDX-FileCopyrightText: 2025 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <string>
#include <string_view>
#include <vector>

class SdFileBrowser {
public:
    struct FileEntry {
        std::string name;
        std::string path;
        bool is_directory = false;
        bool is_parent    = false;
    };

    static constexpr const char* ROOT_PATH             = "/sd";
    static constexpr std::size_t MAX_EXTENSION_FILTERS = 4;

    SdFileBrowser();

    void clear();
    bool refresh(std::string& errorMessage);
    void setViewportRows(std::size_t rows);
    void setExtensionFilter(std::string_view extension);
    void setExtensionFilters(std::initializer_list<std::string_view> extensions);

    bool moveUp();
    bool moveDown();

    bool hasSelection() const;
    const FileEntry* getSelectedEntry() const;
    const FileEntry* getEntry(std::size_t index) const;
    bool enterSelectedDirectory(std::string& errorMessage);
    bool focusPath(const std::string& path, std::string& errorMessage);

    std::size_t getEntryCount() const;
    std::size_t getSelectedIndex() const;
    std::size_t getFirstVisibleIndex() const;
    std::size_t getViewportRows() const;
    const std::string& getCurrentPath() const;

    const std::string& getStatusMessage() const;
    void setStatusMessage(const std::string& message);

    static std::string buildPath(const std::string& fileName);
    static std::string buildPath(const std::string& directoryPath, const std::string& fileName);

private:
    std::vector<FileEntry> _entries;
    std::size_t _selected_index = 0;
    std::size_t _first_visible  = 0;
    std::size_t _viewport_rows  = 1;
    std::string _current_path   = ROOT_PATH;
    std::string _status_message;
    std::array<std::string, MAX_EXTENSION_FILTERS> _extension_filters;
    std::uint8_t _extension_filter_count = 0;

    bool matchesExtension(const std::string& fileName) const;
    bool refreshInternal(const std::string& preferredPath, std::string& errorMessage);
    void ensureSelectionVisible();
};