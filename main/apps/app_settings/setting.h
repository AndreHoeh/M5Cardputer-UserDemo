#pragma once

#include <hal/utils/settings/settings.h>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <variant>

namespace settings_model {

enum class SettingType {
    kBool,
    kInt,
    kFloat,
    kString,
};

using SettingValue = std::variant<bool, int32_t, float, std::string>;

struct NumericRange {
    std::optional<int32_t> int_min;
    std::optional<int32_t> int_max;
    std::optional<float> float_min;
    std::optional<float> float_max;
};

struct SettingDefinition {
    std::string key;
    std::string name;
    SettingType type           = SettingType::kInt;
    SettingValue default_value = int32_t{0};
    NumericRange range;
};

class Setting {
public:
    explicit Setting(SettingDefinition definition) : _definition(std::move(definition))
    {
        _value           = _definition.default_value;
        _persisted_value = _definition.default_value;
        validate(_value);
    }

    const SettingDefinition& definition() const
    {
        return _definition;
    }

    const SettingValue& value() const
    {
        return _value;
    }

    const SettingValue& persistedValue() const
    {
        return _persisted_value;
    }

    bool isDirty() const
    {
        return _value != _persisted_value;
    }

    bool isValid() const
    {
        return _is_valid;
    }

    const std::string& validationMessage() const
    {
        return _validation_message;
    }

    bool setValue(const SettingValue& new_value)
    {
        if (!validate(new_value)) {
            return false;
        }

        _value = new_value;
        return true;
    }

    bool setInt(int32_t value)
    {
        return setValue(SettingValue{value});
    }

    bool getInt(int32_t& out_value) const
    {
        if (!std::holds_alternative<int32_t>(_value)) {
            return false;
        }

        out_value = std::get<int32_t>(_value);
        return true;
    }

    bool load(Settings& settings)
    {
        switch (_definition.type) {
            case SettingType::kBool: {
                const bool default_value = std::holds_alternative<bool>(_definition.default_value)
                                               ? std::get<bool>(_definition.default_value)
                                               : false;
                const bool persisted     = settings.GetBool(_definition.key, default_value);
                _persisted_value         = persisted;
                _value                   = persisted;
                return validate(_value);
            }

            case SettingType::kInt: {
                const int32_t default_value = std::holds_alternative<int32_t>(_definition.default_value)
                                                  ? std::get<int32_t>(_definition.default_value)
                                                  : int32_t{0};
                const int32_t persisted     = settings.GetInt(_definition.key, default_value);
                _persisted_value            = persisted;
                _value                      = persisted;
                return validate(_value);
            }

            case SettingType::kString: {
                const std::string default_value = std::holds_alternative<std::string>(_definition.default_value)
                                                      ? std::get<std::string>(_definition.default_value)
                                                      : std::string{};
                const std::string persisted     = settings.GetString(_definition.key, default_value);
                _persisted_value                = persisted;
                _value                          = persisted;
                return validate(_value);
            }

            case SettingType::kFloat:
            default:
                // Float persistence is intentionally deferred for now.
                _persisted_value    = _definition.default_value;
                _value              = _definition.default_value;
                _is_valid           = true;
                _validation_message = "Float persistence not implemented";
                return false;
        }
    }

    bool save(Settings& settings)
    {
        if (!_is_valid) {
            _validation_message = "Cannot save invalid value";
            return false;
        }

        switch (_definition.type) {
            case SettingType::kBool: {
                if (!std::holds_alternative<bool>(_value)) {
                    _validation_message = "Type mismatch for bool setting";
                    return false;
                }
                settings.SetBool(_definition.key, std::get<bool>(_value));
                _persisted_value    = _value;
                _validation_message = "";
                return true;
            }

            case SettingType::kInt: {
                if (!std::holds_alternative<int32_t>(_value)) {
                    _validation_message = "Type mismatch for int setting";
                    return false;
                }
                settings.SetInt(_definition.key, std::get<int32_t>(_value));
                _persisted_value    = _value;
                _validation_message = "";
                return true;
            }

            case SettingType::kString: {
                if (!std::holds_alternative<std::string>(_value)) {
                    _validation_message = "Type mismatch for string setting";
                    return false;
                }
                settings.SetString(_definition.key, std::get<std::string>(_value));
                _persisted_value    = _value;
                _validation_message = "";
                return true;
            }

            case SettingType::kFloat:
            default:
                _validation_message = "Float persistence not implemented";
                return false;
        }
    }

    void resetToPersisted()
    {
        _value = _persisted_value;
        validate(_value);
    }

private:
    bool validate(const SettingValue& candidate)
    {
        switch (_definition.type) {
            case SettingType::kBool:
                if (!std::holds_alternative<bool>(candidate)) {
                    _is_valid           = false;
                    _validation_message = "Expected bool value";
                    return false;
                }
                break;

            case SettingType::kInt: {
                if (!std::holds_alternative<int32_t>(candidate)) {
                    _is_valid           = false;
                    _validation_message = "Expected int value";
                    return false;
                }

                const int32_t value = std::get<int32_t>(candidate);
                if (_definition.range.int_min.has_value() && value < _definition.range.int_min.value()) {
                    _is_valid           = false;
                    _validation_message = "Value below minimum";
                    return false;
                }
                if (_definition.range.int_max.has_value() && value > _definition.range.int_max.value()) {
                    _is_valid           = false;
                    _validation_message = "Value above maximum";
                    return false;
                }
                break;
            }

            case SettingType::kFloat: {
                if (!std::holds_alternative<float>(candidate)) {
                    _is_valid           = false;
                    _validation_message = "Expected float value";
                    return false;
                }

                const float value = std::get<float>(candidate);
                if (_definition.range.float_min.has_value() && value < _definition.range.float_min.value()) {
                    _is_valid           = false;
                    _validation_message = "Value below minimum";
                    return false;
                }
                if (_definition.range.float_max.has_value() && value > _definition.range.float_max.value()) {
                    _is_valid           = false;
                    _validation_message = "Value above maximum";
                    return false;
                }
                break;
            }

            case SettingType::kString:
                if (!std::holds_alternative<std::string>(candidate)) {
                    _is_valid           = false;
                    _validation_message = "Expected string value";
                    return false;
                }
                break;
        }

        _is_valid           = true;
        _validation_message = "";
        return true;
    }

    SettingDefinition _definition;
    SettingValue _value;
    SettingValue _persisted_value;
    bool _is_valid = true;
    std::string _validation_message;
};

}  // namespace settings_model
