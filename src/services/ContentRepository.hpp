/**
 * @file ContentRepository.hpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 文件内容持久化原语
 *
 * @copyright Copyright (c) 2026
 *
 */

#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

#include <drogon/orm/DbClient.h>

namespace disk::file {

    /**
     * @brief 文件内容持久化原语
     */
    class ContentRepository {
    public:
        explicit ContentRepository(drogon::orm::DbClientPtr db_client);

        [[nodiscard]]
        auto IncrementRefCount(uint64_t content_id) const -> drogon::Task<bool>;

        [[nodiscard]]
        auto CreateContent(
            std::string hash_md5,
            std::string hash_sha256,
            uint64_t size,
            std::filesystem::path storage_path,
            std::string mime_type = ""
        ) const -> drogon::Task<uint64_t>;

    private:
        drogon::orm::DbClientPtr m_db_client;
    };

} ///< namespace disk::file
