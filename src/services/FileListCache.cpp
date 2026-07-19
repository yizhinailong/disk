/**
 * @file FileListCache.cpp
 * @brief Shared Redis file-list cache generation management
 *
 * @copyright Copyright (c) 2026
 */

#include "FileListCache.hpp"

#include <charconv>
#include <string_view>
#include <system_error>

#include "utils/RedisKeyPrefix.hpp"

namespace disk::file {

    auto FileListCache::GetVersion(
        const std::shared_ptr<disk::services::RedisService>& redis_service,
        uint64_t user_id
    ) -> drogon::Task<Result<uint64_t>> {
        if (!redis_service) {
            co_return std::unexpected(ErrorInfo(
                ErrorCode::RedisOperationFailed,
                "Redis service is not initialized"
            ));
        }

        const auto version_key = disk::redis::RedisKeyPrefix::BuildFileListCacheVersionKey(user_id);
        auto version_result = co_await redis_service->Get(version_key);
        if (!version_result) {
            if (version_result.error().code == ErrorCode::RedisKeyNotFound) {
                co_return uint64_t{ 0 };
            }
            co_return std::unexpected(version_result.error());
        }

        const std::string_view encoded_version(*version_result);
        uint64_t version = 0;
        const auto [end, error] = std::from_chars(
            encoded_version.data(),
            encoded_version.data() + encoded_version.size(),
            version
        );
        if (error != std::errc{} || end != encoded_version.data() + encoded_version.size()) {
            Logger::Error() << "Invalid file list cache version: user_id=" << user_id;
            co_return std::unexpected(ErrorInfo(
                ErrorCode::RedisOperationFailed,
                "Invalid file list cache version"
            ));
        }

        co_return version;
    }

    auto FileListCache::Invalidate(
        const std::shared_ptr<disk::services::RedisService>& redis_service,
        uint64_t user_id
    ) -> drogon::Task<void> {
        if (!redis_service) {
            Logger::Error() << "Cannot invalidate file list cache: Redis service is not initialized";
            co_return;
        }

        const auto version_key = disk::redis::RedisKeyPrefix::BuildFileListCacheVersionKey(user_id);
        auto increment_result = co_await redis_service->Incr(version_key);
        if (!increment_result) {
            Logger::Warn() << "Failed to increment file list cache version: user_id=" << user_id;
            co_return;
        }

        Logger::Debug() << "File list cache version incremented: user_id=" << user_id
                        << ", version=" << *increment_result;
    }

} // namespace disk::file
