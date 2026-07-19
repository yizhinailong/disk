/**
 * @file FileListCache.hpp
 * @brief Shared Redis file-list cache generation management
 *
 * @copyright Copyright (c) 2026
 */

#pragma once

#include <cstdint>
#include <memory>

#include "services/RedisService.hpp"
#include "utils/ErrorCode.hpp"

namespace disk::file {

    class FileListCache final {
    public:
        static constexpr int ENTRY_TTL_SECONDS = 30;

        [[nodiscard]]
        static auto GetVersion(
            const std::shared_ptr<disk::services::RedisService>& redis_service,
            uint64_t user_id
        ) -> drogon::Task<Result<uint64_t>>;

        static auto Invalidate(
            const std::shared_ptr<disk::services::RedisService>& redis_service,
            uint64_t user_id
        ) -> drogon::Task<void>;
    };

} // namespace disk::file
