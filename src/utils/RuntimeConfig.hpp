/**
 * @file RuntimeConfig.hpp
 * @brief Runtime JSON configuration loading and environment overrides
 *
 * @copyright Copyright (c) 2026
 */

#pragma once

#include <json/value.h>

namespace disk::utils {

    class RuntimeConfig final {
    public:
        [[nodiscard]]
        static auto LoadFromEnvironment() -> Json::Value;
    };

} // namespace disk::utils
