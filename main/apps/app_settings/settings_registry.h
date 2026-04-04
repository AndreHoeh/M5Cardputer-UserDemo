#pragma once

#include "setting.h"
#include <utility>
#include <vector>

namespace settings_model {

class SettingsRegistry {
public:
    void registerSetting(SettingDefinition definition)
    {
        _settings.emplace_back(std::move(definition));
    }

    bool loadAll(Settings& settings)
    {
        bool all_loaded = true;
        for (auto& setting : _settings) {
            all_loaded = setting.load(settings) && all_loaded;
        }
        return all_loaded;
    }

    bool saveDirty(Settings& settings)
    {
        bool all_saved = true;
        for (auto& setting : _settings) {
            if (!setting.isDirty()) {
                continue;
            }
            all_saved = setting.save(settings) && all_saved;
        }
        return all_saved;
    }

    Setting* findByKey(const std::string& key)
    {
        for (auto& setting : _settings) {
            if (setting.definition().key == key) {
                return &setting;
            }
        }
        return nullptr;
    }

    const Setting* findByKey(const std::string& key) const
    {
        for (const auto& setting : _settings) {
            if (setting.definition().key == key) {
                return &setting;
            }
        }
        return nullptr;
    }

    size_t size() const
    {
        return _settings.size();
    }

    Setting* at(size_t index)
    {
        if (index >= _settings.size()) {
            return nullptr;
        }
        return &_settings[index];
    }

private:
    std::vector<Setting> _settings;
};

}  // namespace settings_model
