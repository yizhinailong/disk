/**
 * @file DbRowUtils.hpp
 * @brief Shared helpers for reading database row values
 *
 * @copyright Copyright (c) 2026
 */

#pragma once

#include <optional>

namespace disk::utils {

    template <typename T, typename Row>
    [[nodiscard]] auto OptionalRowValue(const Row& row, const char* field)
        -> std::optional<T> {
        return row[field].isNull() ? std::nullopt :
                                     std::optional(row[field].template as<T>());
    }

} // namespace disk::utils
