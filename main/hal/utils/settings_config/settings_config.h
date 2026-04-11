#pragma once

#include <cstddef>
#include <string>

class Hal;

namespace settings_config {

std::size_t loadFromFile(Hal& hal, const char* filePath, const std::string& logTag);

}  // namespace settings_config