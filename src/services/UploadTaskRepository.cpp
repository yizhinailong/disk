/**
 * @file UploadTaskRepository.cpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 上传任务持久化原语实现
 *
 * @copyright Copyright (c) 2026
 *
 */

#include "UploadTaskRepository.hpp"

namespace disk::file {

    UploadTaskRepository::UploadTaskRepository(drogon::orm::DbClientPtr db_client)
        : m_db_client(std::move(db_client)) {
    }

    auto UploadTaskRepository::MarkCompleted(std::string const& upload_id) const -> drogon::Task<bool> {
        auto finalize_result = co_await m_db_client->execSqlCoro(
            "UPDATE upload_tasks SET status = 1, finalized_at = NOW() WHERE id = $1 AND status = 0",
            upload_id
        );
        co_return finalize_result.affectedRows() > 0;
    }

    auto UploadTaskRepository::DeleteChunks(std::string const& upload_id) const -> drogon::Task<void> {
        co_await m_db_client->execSqlCoro(
            "DELETE FROM upload_task_chunks WHERE task_id = $1",
            upload_id
        );
    }

} ///< namespace disk::file
