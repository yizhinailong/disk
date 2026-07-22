/**
 * @file CleanupService.cpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 系统清理服务实现
 *
 * @copyright Copyright (c) 2026
 *
 */

#include "CleanupService.hpp"

#include <algorithm>
#include <chrono>
#include <optional>
#include <unordered_map>
#include <utility>
#include <vector>

#include <drogon/orm/CoroMapper.h>
#include <drogon/orm/Criteria.h>

#include "TrashService.hpp"
#include "models/FileContents.hpp"
#include "models/Trash.hpp"
#include "models/UploadTasks.hpp"
#include "models/Users.hpp"
#include "services/ContentService.hpp"
#include "services/FileServiceUtils.hpp"
#include "services/TrashContentIdResolver.hpp"
#include "services/UploadLifecycleService.hpp"
#include "storage/BlobStoreMgr.hpp"
#include "storage/StorageMgr.hpp"
#include "utils/BatchUtils.hpp"

namespace disk::services {

    using disk::utils::BatchUtils;
    using disk::utils::DEFAULT_BATCH_CHUNK_SIZE;
    using drogon::orm::CompareOperator;
    using drogon::orm::CoroMapper;
    using drogon::orm::Criteria;
    using drogon_model::disk::FileContents;

    constexpr int kUploadTaskCleanupBatchSize = 100;
    constexpr int kTrashFetchBatchSize = 100;
    constexpr int kMaxTrashBatchesPerRun = 20;

    CleanupService::CleanupService(drogon::orm::DbClientPtr db_client)
        : m_db_client(std::move(db_client)) {
        Logger::Debug() << "CleanupService initialization completed";
    }

    auto CleanupService::RunExpiredCleanupOnce(disk::utils::LogContext log_context)
        -> drogon::Task<Result<CleanupRunResult>> {
        auto trash_result = co_await CleanupExpiredTrash(log_context);
        auto upload_result = co_await CleanupExpiredUploadTasks(log_context);

        if (!trash_result) {
            co_return std::unexpected(trash_result.error());
        }
        if (!upload_result) {
            co_return std::unexpected(upload_result.error());
        }

        co_return CleanupRunResult{
            .expired_trash_deleted = trash_result.value(),
            .expired_upload_tasks_cleaned = upload_result.value(),
        };
    }

    auto CleanupService::CleanupExpiredTrash(disk::utils::LogContext log_context)
        -> drogon::Task<Result<int>> {
        disk::trash::TrashService trash_service(m_db_client);
        co_return co_await trash_service.CleanupExpiredTrashItems(
            kTrashFetchBatchSize,
            kMaxTrashBatchesPerRun,
            std::move(log_context)
        );
    }

    auto CleanupService::CleanupExpiredUploadTasks(disk::utils::LogContext log_context)
        -> drogon::Task<Result<int>> {
        Logger::Info(log_context) << "Starting cleanup of expired upload tasks";

        auto batch_start = std::chrono::steady_clock::now();
        try {
            disk::upload::UploadLifecycleService lifecycle_service(
                m_db_client,
                disk::storage::StorageMgr::GetStorage(),
                disk::storage::StorageMgr::GetUploadStagingStorage(),
                disk::storage::BlobStoreMgr::GetBlobStore()
            );
            auto cleanup_result = co_await lifecycle_service.ExpireInProgressUploads(
                kUploadTaskCleanupBatchSize,
                log_context
            );
            if (!cleanup_result) {
                co_return std::unexpected(cleanup_result.error());
            }

            auto cleaned_count = static_cast<int>(cleanup_result->expired);
            Logger::Info(log_context)
                << "[cleanup_batch] upload_tasks cleaned_count=" << cleaned_count
                << " batch_duration_ms="
                << std::chrono::duration_cast<std::chrono::milliseconds>(
                       std::chrono::steady_clock::now() - batch_start
                   )
                       .count();

            Logger::Info(log_context)
                << "Upload task cleanup completed: cleaned_count=" << cleaned_count;
            co_return cleaned_count;

        } catch (const drogon::orm::DrogonDbException& e) {
            Logger::Error(log_context)
                << "Database error cleaning expired upload tasks: " << e.base().what();
            co_return std::unexpected(
                ErrorInfo(ErrorCode::InternalError, "Failed to clean expired upload tasks")
            );
        } catch (const std::exception& e) {
            Logger::Error(log_context)
                << "Unknown error cleaning expired upload tasks: " << e.what();
            co_return std::unexpected(
                ErrorInfo(ErrorCode::InternalError, "Failed to clean expired upload tasks")
            );
        }
    }

} // namespace disk::services
