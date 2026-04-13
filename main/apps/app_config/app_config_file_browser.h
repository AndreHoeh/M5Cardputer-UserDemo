/*
 * SPDX-FileCopyrightText: 2025 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <cstddef>
#include <string>
#include <vector>

class AppConfigFileBrowser {
public:
    struct FileEntry {
        std::string name;
        std::string path;
    };

    static constexpr const char* ROOT_PATH = "/sdcard";

    AppConfigFileBrowser();

    void clear();
    bool refresh(std::string& errorMessage);
    void setViewportRows(std::size_t rows);

    bool moveUp();
    bool moveDown();

    bool hasSelection() const;
    const FileEntry* getSelectedEntry() const;
    const FileEntry* getEntry(std::size_t index) const;

    std::size_t getEntryCount() const;
    std::size_t getSelectedIndex() const;
    std::size_t getFirstVisibleIndex() const;
    std::size_t getViewportRows() const;

    const std::string& getStatusMessage() const;
    void setStatusMessage(const std::string& message);

    bool createFile(const std::string& fileName, std::string& createdPath, std::string& errorMessage);
    bool deleteSelectedFile(std::string& deletedPath, std::string& errorMessage);

private:
    std::vector<FileEntry> _entries;
    std::size_t _selected_index = 0;
    std::size_t _first_visible  = 0;
    std::size_t _viewport_rows  = 1;
    std::string _status_message;

    static std::string buildPath(const std::string& fileName);
    bool refreshInternal(const std::string& preferredPath, std::string& errorMessage);
    void ensureSelectionVisible();
    bool isValidFileName(const std::string& fileName, std::string& errorMessage) const;
    void setDefaultStatus();
};