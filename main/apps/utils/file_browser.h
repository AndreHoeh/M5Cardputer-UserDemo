/*
 * SPDX-FileCopyrightText: 2025 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <cstddef>
#include <string>
#include <vector>

class SdFileBrowser {
public:
    struct FileEntry {
        std::string name;
        std::string path;
        bool is_directory = false;
        bool is_parent    = false;
    };

    static constexpr const char* ROOT_PATH = "/sd";

    SdFileBrowser();

    void clear();
    bool refresh(std::string& errorMessage);
    void setViewportRows(std::size_t rows);
    void setExtensionFilter(std::string extension);
    const std::string& getExtensionFilter() const;

    bool moveUp();
    bool moveDown();

    bool hasSelection() const;
    const FileEntry* getSelectedEntry() const;
    const FileEntry* getEntry(std::size_t index) const;
    bool enterSelectedDirectory(std::string& errorMessage);

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
    std::string _extension_filter;

    bool matchesExtension(const std::string& fileName) const;
    bool refreshInternal(const std::string& preferredPath, std::string& errorMessage);
    void ensureSelectionVisible();
};