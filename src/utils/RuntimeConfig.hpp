/**
 * @file RuntimeConfig.hpp
 * @brief Runtime JSON configuration loading and environment overrides
 *
 * @copyright Copyright (c) 2026
 */

#pragma once

#include <string_view>

#include <json/value.h>

namespace disk::utils {

    class RuntimeConfig final {
    public:
        [[nodiscard]]
        static auto LoadFromEnvironment() -> Json::Value;

        [[nodiscard]]
        static auto LoadFile(std::string_view path) -> Json::Value;

        static auto ApplyEnvironmentOverrides(Json::Value& config) -> void;
    };

} // namespace disk::utils
