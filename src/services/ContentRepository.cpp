/**
 * @file ContentRepository.cpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 文件内容持久化原语实现
 *
 * @copyright Copyright (c) 2026
 *
 */

#include "ContentRepository.hpp"

#include <drogon/orm/CoroMapper.h>

#include "models/FileContents.hpp"

namespace disk::file {

    using drogon::orm::CoroMapper;
    using drogon_model::disk::FileContents;

    ContentRepository::ContentRepository(drogon::orm::DbClientPtr db_client)
        : m_db_client(std::move(db_client)) {
    }

    auto ContentRepository::IncrementRefCount(uint64_t content_id) const -> drogon::Task<bool> {
        auto increment_result = co_await m_db_client->execSqlCoro(
            "UPDATE file_contents SET ref_count = ref_count + 1 WHERE id = $1",
            content_id
        );
        co_return increment_result.affectedRows() > 0;
    }

    auto ContentRepository::CreateContent(
        std::string hash_md5,
        std::string hash_sha256,
        uint64_t size,
        std::filesystem::path storage_path,
        std::string mime_type
    ) const -> drogon::Task<uint64_t> {
        CoroMapper<FileContents> content_mapper(m_db_client);

        FileContents content;
        content.setHashMd5(std::move(hash_md5));
        content.setHashSha256(std::move(hash_sha256));
        content.setSize(size);
        content.setStoragePath(storage_path.string());
        content.setMimeType(std::move(mime_type));
        content.setRefCount(1);

        content = co_await content_mapper.insert(content);
        co_return content.getValueOfId();
    }

} ///< namespace disk::file
