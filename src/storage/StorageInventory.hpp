/**
 * @file StorageInventory.hpp
 * @brief 有界存储对象清单契约
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "utils/ErrorCode.hpp"

namespace disk::storage {

    inline constexpr size_t kMaxStorageInventoryPageSize = 1000;

    struct StorageInventoryObject {
        std::string locator;
        uint64_t size_bytes{ 0 };
    };

    struct StorageInventoryPage {
        std::vector<StorageInventoryObject> objects;
        std::string continuation_token;
        bool has_more{ false };
    };

    enum class LocalInventoryLocator {
        RelativeToRoot,
        IncludeRoot,
    };

    [[nodiscard]]
    auto ListLocalStorageInventory(
        const std::filesystem::path& root,
        const std::string& continuation_token,
        size_t limit,
        LocalInventoryLocator locator_mode
    ) -> Result<StorageInventoryPage>;

} // namespace disk::storage
