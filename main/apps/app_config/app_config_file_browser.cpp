/*
 * SPDX-FileCopyrightText: 2025 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "app_config_file_browser.h"
#include <algorithm>
#include <cerrno>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <sys/stat.h>

namespace {
std::string to_lower_copy(const std::string& value)
{
    std::string lowered = value;
    std::transform(lowered.begin(), lowered.end(), lowered.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return lowered;
}
}  // namespace

AppConfigFileBrowser::AppConfigFileBrowser()
{
    setDefaultStatus();
}

void AppConfigFileBrowser::clear()
{
    _entries.clear();
    _selected_index = 0;
    _first_visible  = 0;
}

bool AppConfigFileBrowser::refresh(std::string& errorMessage)
{
    const std::string preferredPath = hasSelection() ? _entries[_selected_index].path : std::string();
    return refreshInternal(preferredPath, errorMessage);
}

void AppConfigFileBrowser::setViewportRows(std::size_t rows)
{
    _viewport_rows = std::max<std::size_t>(rows, 1);
    ensureSelectionVisible();
}

void AppConfigFileBrowser::setExtensionFilter(std::string extension)
{
    _extension_filter = to_lower_copy(extension);
}

const std::string& AppConfigFileBrowser::getExtensionFilter() const
{
    return _extension_filter;
}

bool AppConfigFileBrowser::moveUp()
{
    if (_entries.empty() || _selected_index == 0) {
        return false;
    }

    --_selected_index;
    ensureSelectionVisible();
    return true;
}

bool AppConfigFileBrowser::moveDown()
{
    if (_entries.empty() || _selected_index + 1 >= _entries.size()) {
        return false;
    }

    ++_selected_index;
    ensureSelectionVisible();
    return true;
}

bool AppConfigFileBrowser::hasSelection() const
{
    return !_entries.empty() && _selected_index < _entries.size();
}

const AppConfigFileBrowser::FileEntry* AppConfigFileBrowser::getSelectedEntry() const
{
    if (!hasSelection()) {
        return nullptr;
    }

    return &_entries[_selected_index];
}

const AppConfigFileBrowser::FileEntry* AppConfigFileBrowser::getEntry(std::size_t index) const
{
    if (index >= _entries.size()) {
        return nullptr;
    }

    return &_entries[index];
}

std::size_t AppConfigFileBrowser::getEntryCount() const
{
    return _entries.size();
}

std::size_t AppConfigFileBrowser::getSelectedIndex() const
{
    return _selected_index;
}

std::size_t AppConfigFileBrowser::getFirstVisibleIndex() const
{
    return _first_visible;
}

std::size_t AppConfigFileBrowser::getViewportRows() const
{
    return _viewport_rows;
}

const std::string& AppConfigFileBrowser::getStatusMessage() const
{
    return _status_message;
}

void AppConfigFileBrowser::setStatusMessage(const std::string& message)
{
    _status_message = message;
}

bool AppConfigFileBrowser::createFile(const std::string& fileName, std::string& createdPath, std::string& errorMessage)
{
    if (!isValidFileName(fileName, errorMessage)) {
        return false;
    }

    const std::string path = buildPath(fileName);
    struct stat fileInfo   = {};
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
    if (!refreshInternal(path, errorMessage)) {
        return false;
    }

    _status_message = "Created " + fileName;
    return true;
}

bool AppConfigFileBrowser::deleteSelectedFile(std::string& deletedPath, std::string& errorMessage)
{
    if (!hasSelection()) {
        errorMessage = "no file selected";
        return false;
    }

    const std::size_t deletedIndex = _selected_index;
    const std::string deletedName  = _entries[_selected_index].name;
    deletedPath                    = _entries[_selected_index].path;

    if (std::remove(deletedPath.c_str()) != 0) {
        errorMessage = std::strerror(errno);
        return false;
    }

    std::string refreshError;
    if (!refreshInternal(std::string(), refreshError)) {
        errorMessage = refreshError;
        return false;
    }

    if (hasSelection()) {
        _selected_index = std::min(deletedIndex, _entries.size() - 1);
        ensureSelectionVisible();
    }

    _status_message = "Deleted " + deletedName;
    return true;
}

std::string AppConfigFileBrowser::buildPath(const std::string& fileName)
{
    return std::string(ROOT_PATH) + "/" + fileName;
}

bool AppConfigFileBrowser::matchesExtension(const std::string& fileName) const
{
    if (_extension_filter.empty()) {
        return true;
    }

    const std::string lowered = to_lower_copy(fileName);
    if (lowered.size() < _extension_filter.size()) {
        return false;
    }

    return lowered.compare(lowered.size() - _extension_filter.size(), _extension_filter.size(), _extension_filter) == 0;
}

bool AppConfigFileBrowser::refreshInternal(const std::string& preferredPath, std::string& errorMessage)
{
    DIR* directory = opendir(ROOT_PATH);
    if (directory == nullptr) {
        errorMessage = std::strerror(errno);
        return false;
    }

    std::vector<FileEntry> entries;
    dirent* entry = nullptr;
    while ((entry = readdir(directory)) != nullptr) {
        const std::string name = entry->d_name;
        if (name == "." || name == "..") {
            continue;
        }

        const std::string path = buildPath(name);
        struct stat fileInfo   = {};
        if (stat(path.c_str(), &fileInfo) != 0 || !S_ISREG(fileInfo.st_mode)) {
            continue;
        }

        if (!matchesExtension(name)) {
            continue;
        }

        entries.push_back({name, path});
    }

    closedir(directory);

    std::sort(entries.begin(), entries.end(), [](const FileEntry& left, const FileEntry& right) {
        const std::string leftName  = to_lower_copy(left.name);
        const std::string rightName = to_lower_copy(right.name);
        if (leftName == rightName) {
            return left.name < right.name;
        }
        return leftName < rightName;
    });

    _entries = std::move(entries);
    if (_entries.empty()) {
        _selected_index = 0;
        _first_visible  = 0;
        if (_extension_filter.empty()) {
            _status_message = "N New  No files in /sdcard";
        } else {
            _status_message = "No matching files in /sdcard";
        }
        return true;
    }

    _selected_index = 0;
    if (!preferredPath.empty()) {
        for (std::size_t index = 0; index < _entries.size(); ++index) {
            if (_entries[index].path == preferredPath) {
                _selected_index = index;
                break;
            }
        }
    }

    ensureSelectionVisible();
    setDefaultStatus();
    return true;
}

void AppConfigFileBrowser::ensureSelectionVisible()
{
    if (_entries.empty()) {
        _first_visible = 0;
        return;
    }

    if (_selected_index < _first_visible) {
        _first_visible = _selected_index;
    } else if (_selected_index >= _first_visible + _viewport_rows) {
        _first_visible = _selected_index - _viewport_rows + 1;
    }
}

bool AppConfigFileBrowser::isValidFileName(const std::string& fileName, std::string& errorMessage) const
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

void AppConfigFileBrowser::setDefaultStatus()
{
    _status_message = "N New  Enter Open  D Delete";
}