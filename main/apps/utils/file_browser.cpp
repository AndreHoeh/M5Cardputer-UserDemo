/*
 * SPDX-FileCopyrightText: 2025 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "file_browser.h"

#include <algorithm>
#include <cerrno>
#include <cctype>
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

std::string parent_path(const std::string& path)
{
    if (path == SdFileBrowser::ROOT_PATH) {
        return path;
    }

    const std::size_t separator = path.find_last_of('/');
    if (separator == std::string::npos || separator == 0) {
        return SdFileBrowser::ROOT_PATH;
    }

    if (path.compare(0, std::strlen(SdFileBrowser::ROOT_PATH), SdFileBrowser::ROOT_PATH) != 0) {
        return SdFileBrowser::ROOT_PATH;
    }

    return path.substr(0, separator);
}
}  // namespace

SdFileBrowser::SdFileBrowser() = default;

void SdFileBrowser::clear()
{
    _entries.clear();
    _selected_index = 0;
    _first_visible  = 0;
}

bool SdFileBrowser::refresh(std::string& errorMessage)
{
    const std::string preferredPath = hasSelection() ? _entries[_selected_index].path : std::string();
    return refreshInternal(preferredPath, errorMessage);
}

void SdFileBrowser::setViewportRows(std::size_t rows)
{
    _viewport_rows = std::max<std::size_t>(rows, 1);
    ensureSelectionVisible();
}

void SdFileBrowser::setExtensionFilter(std::string extension)
{
    _extension_filter = to_lower_copy(extension);
}

const std::string& SdFileBrowser::getExtensionFilter() const
{
    return _extension_filter;
}

bool SdFileBrowser::moveUp()
{
    if (_entries.empty() || _selected_index == 0) {
        return false;
    }

    --_selected_index;
    ensureSelectionVisible();
    return true;
}

bool SdFileBrowser::moveDown()
{
    if (_entries.empty() || _selected_index + 1 >= _entries.size()) {
        return false;
    }

    ++_selected_index;
    ensureSelectionVisible();
    return true;
}

bool SdFileBrowser::hasSelection() const
{
    return !_entries.empty() && _selected_index < _entries.size();
}

const SdFileBrowser::FileEntry* SdFileBrowser::getSelectedEntry() const
{
    if (!hasSelection()) {
        return nullptr;
    }

    return &_entries[_selected_index];
}

const SdFileBrowser::FileEntry* SdFileBrowser::getEntry(std::size_t index) const
{
    if (index >= _entries.size()) {
        return nullptr;
    }

    return &_entries[index];
}

bool SdFileBrowser::enterSelectedDirectory(std::string& errorMessage)
{
    const FileEntry* entry = getSelectedEntry();
    if (entry == nullptr) {
        errorMessage = "no entry selected";
        return false;
    }

    if (!entry->is_directory) {
        errorMessage = "selected entry is not a directory";
        return false;
    }

    _current_path = entry->path;
    return refreshInternal(std::string(), errorMessage);
}

std::size_t SdFileBrowser::getEntryCount() const
{
    return _entries.size();
}

std::size_t SdFileBrowser::getSelectedIndex() const
{
    return _selected_index;
}

std::size_t SdFileBrowser::getFirstVisibleIndex() const
{
    return _first_visible;
}

std::size_t SdFileBrowser::getViewportRows() const
{
    return _viewport_rows;
}

const std::string& SdFileBrowser::getCurrentPath() const
{
    return _current_path;
}

const std::string& SdFileBrowser::getStatusMessage() const
{
    return _status_message;
}

void SdFileBrowser::setStatusMessage(const std::string& message)
{
    _status_message = message;
}

std::string SdFileBrowser::buildPath(const std::string& fileName)
{
    return std::string(ROOT_PATH) + "/" + fileName;
}

std::string SdFileBrowser::buildPath(const std::string& directoryPath, const std::string& fileName)
{
    if (directoryPath.empty() || directoryPath == ROOT_PATH) {
        return buildPath(fileName);
    }

    if (!directoryPath.empty() && directoryPath.back() == '/') {
        return directoryPath + fileName;
    }

    return directoryPath + "/" + fileName;
}

bool SdFileBrowser::matchesExtension(const std::string& fileName) const
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

bool SdFileBrowser::refreshInternal(const std::string& preferredPath, std::string& errorMessage)
{
    DIR* directory = opendir(_current_path.c_str());
    if (directory == nullptr) {
        errorMessage = std::strerror(errno);
        return false;
    }

    std::vector<FileEntry> entries;
    if (_current_path != ROOT_PATH) {
        entries.push_back({"..", parent_path(_current_path), true, true});
    }

    dirent* entry = nullptr;
    while ((entry = readdir(directory)) != nullptr) {
        const std::string name = entry->d_name;
        if (name == "." || name == "..") {
            continue;
        }

        const std::string path = buildPath(_current_path, name);
        struct stat fileInfo   = {};
        if (stat(path.c_str(), &fileInfo) != 0) {
            continue;
        }

        if (S_ISDIR(fileInfo.st_mode)) {
            entries.push_back({name, path, true, false});
            continue;
        }

        if (!S_ISREG(fileInfo.st_mode)) {
            continue;
        }

        if (!matchesExtension(name)) {
            continue;
        }

        entries.push_back({name, path, false, false});
    }

    closedir(directory);

    std::sort(entries.begin(), entries.end(), [](const FileEntry& left, const FileEntry& right) {
        if (left.is_parent != right.is_parent) {
            return left.is_parent;
        }

        if (left.is_directory != right.is_directory) {
            return left.is_directory;
        }

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
            _status_message = "No files in " + _current_path;
        } else {
            _status_message = "No matching files in " + _current_path;
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
    return true;
}

void SdFileBrowser::ensureSelectionVisible()
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