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
void normalize_lower_in_place(std::string& value)
{
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
}

bool ends_with_ignore_case(const std::string& value, const std::string& suffix)
{
    if (value.size() < suffix.size()) {
        return false;
    }

    const std::size_t offset = value.size() - suffix.size();
    for (std::size_t index = 0; index < suffix.size(); ++index) {
        const unsigned char valueChar  = static_cast<unsigned char>(value[offset + index]);
        const unsigned char suffixChar = static_cast<unsigned char>(suffix[index]);
        if (std::tolower(valueChar) != std::tolower(suffixChar)) {
            return false;
        }
    }

    return true;
}

bool comes_before_ignore_case(const std::string& left, const std::string& right)
{
    const std::size_t compareLength = std::min(left.size(), right.size());
    for (std::size_t index = 0; index < compareLength; ++index) {
        const unsigned char leftChar  = static_cast<unsigned char>(left[index]);
        const unsigned char rightChar = static_cast<unsigned char>(right[index]);
        const int leftLower           = std::tolower(leftChar);
        const int rightLower          = std::tolower(rightChar);
        if (leftLower != rightLower) {
            return leftLower < rightLower;
        }
    }

    if (left.size() == right.size()) {
        return left < right;
    }

    return left.size() < right.size();
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

void SdFileBrowser::setExtensionFilter(std::string_view extension)
{
    setExtensionFilters({extension});
}

void SdFileBrowser::setExtensionFilters(std::initializer_list<std::string_view> extensions)
{
    for (std::string& extension : _extension_filters) {
        extension.clear();
    }

    _extension_filter_count = 0;
    for (std::string_view extension : extensions) {
        if (extension.empty()) {
            continue;
        }

        if (_extension_filter_count >= _extension_filters.size()) {
            break;
        }

        _extension_filters[_extension_filter_count] = extension;
        normalize_lower_in_place(_extension_filters[_extension_filter_count]);
        ++_extension_filter_count;
    }
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

bool SdFileBrowser::focusPath(const std::string& path, std::string& errorMessage)
{
    if (path.empty()) {
        errorMessage = "path is empty";
        return false;
    }

    const std::size_t separator = path.find_last_of('/');
    if (separator == std::string::npos) {
        errorMessage = "invalid path";
        return false;
    }

    const std::string directoryPath = (separator == 0) ? std::string(ROOT_PATH) : path.substr(0, separator);
    _current_path                   = directoryPath.empty() ? std::string(ROOT_PATH) : directoryPath;
    return refreshInternal(path, errorMessage);
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
    if (_extension_filter_count == 0) {
        return true;
    }

    for (std::uint8_t index = 0; index < _extension_filter_count; ++index) {
        if (ends_with_ignore_case(fileName, _extension_filters[index])) {
            return true;
        }
    }

    return false;
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

        return comes_before_ignore_case(left.name, right.name);
    });

    _entries = std::move(entries);
    if (_entries.empty()) {
        _selected_index = 0;
        _first_visible  = 0;
        if (_extension_filter_count == 0) {
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