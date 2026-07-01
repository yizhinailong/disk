/**
 * @file UploadTaskRepository.hpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 上传任务持久化原语
 *
 * @copyright Copyright (c) 2026
 *
 */

#pragma once

#include <string>

#include <drogon/orm/DbClient.h>

namespace disk::file {

    /**
     * @brief 上传任务持久化原语
     */
    class UploadTaskRepository {
    public:
        explicit UploadTaskRepository(drogon::orm::DbClientPtr db_client);

        [[nodiscard]]
        auto MarkCompleted(std::string const& upload_id) const -> drogon::Task<bool>;

        auto DeleteChunks(std::string const& upload_id) const -> drogon::Task<void>;

    private:
        drogon::orm::DbClientPtr m_db_client;
    };

} ///< namespace disk::file
