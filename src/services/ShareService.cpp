/**
 * @file ShareService.cpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 分享服务实现
 *
 * @copyright Copyright (c) 2026
 *
 */

#include "services/ShareService.hpp"

#include <algorithm>
#include <chrono>
#include <random>
#include <stdexcept>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

#include <drogon/orm/CoroMapper.h>
#include <trantor/utils/Date.h>

#include "models/Files.hpp"
#include "models/Folders.hpp"
#include "models/ShareFiles.hpp"
#include "models/Shares.hpp"
#include "services/ContentService.hpp"
#include "services/FileListCache.hpp"
#include "services/TransactionRunner.hpp"
#include "utils/BatchUtils.hpp"
#include "utils/HashUtil.hpp"
#include "utils/RedisKeyPrefix.hpp"

namespace disk::share {

    using namespace drogon::orm;
    using disk::error::Result;
    using disk::utils::BatchUtils;
    using disk::utils::DEFAULT_BATCH_CHUNK_SIZE;
    using drogon_model::disk::Files;
    using drogon_model::disk::Folders;
    using drogon_model::disk::ShareFiles;
    using drogon_model::disk::Shares;

    namespace {
        constexpr int SHARE_ACCESS_FAILURE_LIMIT = 5;
        constexpr int SHARE_ACCESS_FAILURE_WINDOW_SECONDS = 900;
        constexpr std::string_view SHARE_ACCESS_VALIDATION_ERROR_MESSAGE =
            "Share access validation failed";
        constexpr std::string_view SHARE_ACCESS_RATE_LIMIT_ERROR_MESSAGE =
            "Too many password verification attempts, please try again later";

        [[nodiscard]] auto BuildSharedFolderAccessPredicate(const std::string& folder_alias, size_t start_index = 1)
            -> std::string {
            return "EXISTS (" "SELECT 1 FROM share_files sff " "JOIN folders shared_root ON sff.item_id = shared_root.id " "WHERE sff.share_id = $" + std::to_string(start_index) + " AND sff.item_type = 'folder' " "AND " + folder_alias + ".user_id = shared_root.user_id " "AND (" + folder_alias + ".id = shared_root.id OR " + folder_alias + ".path LIKE CONCAT(shared_root.path, '%'))" ")";
        }

        [[nodiscard]] auto BuildSharedFileAccessPredicate(const std::string& file_alias, size_t start_index = 1)
            -> std::string {
            return "(EXISTS (" "SELECT 1 FROM share_files sff " "WHERE sff.share_id = $" + std::to_string(start_index) + " AND sff.item_type = 'file' AND sff.item_id = " + file_alias + ".id" ") OR EXISTS (" "SELECT 1 FROM share_files sff " "JOIN folders shared_root ON sff.item_id = shared_root.id " "JOIN folders parent_folder ON parent_folder.id = " + file_alias + ".folder_id " "WHERE sff.share_id = $" + std::to_string(start_index + 1) + " AND sff.item_type = 'folder' " "AND parent_folder.user_id = shared_root.user_id " "AND parent_folder.path LIKE CONCAT(shared_root.path, '%')" "))";
        }

        [[nodiscard]] auto BuildFilePath(const std::string& folder_path, const std::string& filename)
            -> std::string {
            return folder_path == "/" ? "/" + filename : folder_path + filename;
        }

        [[nodiscard]] auto BuildFolderPath(const std::string& parent_path, const std::string& name)
            -> std::string {
            return parent_path == "/" ? "/" + name + "/" : parent_path + name + "/";
        }

        template <typename BindParameters>
        auto ExecSqlWithBindings(
            const drogon::orm::DbClientPtr& client,
            const std::string& sql,
            BindParameters bind_parameters
        ) -> drogon::Task<drogon::orm::Result> {
            auto binder = *client << sql;
            bind_parameters(binder);
            co_return co_await drogon::orm::internal::SqlAwaiter(std::move(binder));
        }
    } // namespace

    /// ==================== 构造函数 ====================

    ShareService::ShareService(
        DbClientPtr db_client,
        drogon::nosql::RedisClientPtr redis_client,
        std::string jwt_secret
    )
        : m_db_client(std::move(db_client)),
          m_redis_client(std::move(redis_client)),
          m_redis_service(disk::services::RedisService::GetInstance()),
          m_jwt_secret(std::move(jwt_secret)),
          m_audit_service(m_db_client) {
        /// 初始化 RedisService 单例（如果尚未初始化）
        disk::services::RedisService::Initialize(m_redis_client);
        Logger::Debug(disk::utils::ServiceRuntimeLogContext()) << "Service initialized: service=share";
    }

    /// ==================== 公共方法 ====================

    auto ShareService::Create(
        CreateShareRequest request,
        uint64_t user_id,
        const ShareAuditContext& audit_context,
        disk::utils::LogContext log_context
    )
        -> drogon::Task<Result<CreateShareResponse>> {
        Logger::Info(log_context)
            << "Creating share: user_id=" << user_id
            << ", file_ids.size()=" << request.file_ids.size()
            << ", folder_ids.size()=" << request.folder_ids.size();

        /// 1. 验证文件和文件夹所有权
        std::vector<Files> files;
        if (!request.file_ids.empty()) {
            auto files_result =
                co_await ValidateFileOwnership(request.file_ids, user_id, log_context);
            if (!files_result) {
                co_return std::unexpected(files_result.error());
            }
            files = std::move(*files_result);
        }

        std::vector<Folders> folders;
        if (!request.folder_ids.empty()) {
            auto folders_result =
                co_await ValidateFolderOwnership(request.folder_ids, user_id, log_context);
            if (!folders_result) {
                co_return std::unexpected(folders_result.error());
            }
            folders = std::move(*folders_result);
        }

        /// 2. 生成分享码
        auto share_code = GenerateShareCode();

        /// 3. 计算过期时间
        auto now = trantor::Date::now();
        std::optional<trantor::Date> expires_at;
        if (request.expire_days > 0) {
            expires_at = now.after(request.expire_days * 86400);
        }

        /// 4. 哈希密码（如果有）
        std::optional<std::string> password_hash;
        if (request.password.has_value() && !request.password->empty()) {
            auto hash_result = utils::HashUtil::HashPassword(*request.password);
            if (!hash_result) {
                Logger::Error(log_context) << "Password hashing failed";
                co_return std::unexpected(
                    ErrorInfo(ErrorCode::InternalError, "Password encryption failed")
                );
            }
            password_hash = *hash_result;
        }

        /// 5. 创建分享记录 + 分享文件关联（事务保证原子性）
        Shares share;
        share.setShareCode(share_code);
        share.setUserId(user_id);
        if (password_hash.has_value()) {
            share.setPasswordHash(*password_hash);
        }
        share.setPermission(SharePermissionToString(request.permission));
        share.setViewCount(0);
        share.setDownloadCount(0);
        share.setStatus(static_cast<int8_t>(ShareStatus::Active));
        if (expires_at.has_value()) {
            share.setExpiresAt(*expires_at);
        }
        share.setCreatedAt(now);
        share.setUpdatedAt(now);

        Shares created_share;
        std::shared_ptr<drogon::orm::Transaction> transaction;
        try {
            transaction = co_await disk::file::TransactionRunner::Begin(m_db_client);

            /// 插入分享行
            CoroMapper<Shares> share_mapper(transaction);
            created_share = co_await share_mapper.insert(share);

            /// 批量插入 share_files 关联
            if (!files.empty()) {
                auto chunks = BatchUtils::Chunk(files, DEFAULT_BATCH_CHUNK_SIZE);
                for (const auto& chunk : chunks) {
                    std::string insert_sql =
                        "INSERT INTO share_files (share_id, item_type, item_id, created_at) VALUES ";
                    for (size_t i = 0; i < chunk.size(); ++i) {
                        if (i > 0) {
                            insert_sql += ", ";
                        }
                        insert_sql += "($" + std::to_string(i * 4 + 1) + ", $" + std::to_string(i * 4 + 2) + ", $" + std::to_string(i * 4 + 3) + ", $" + std::to_string(i * 4 + 4) + ")";
                    }

                    co_await ExecSqlWithBindings(
                        transaction,
                        insert_sql,
                        [&](auto& binder) {
                            for (const auto& file : chunk) {
                                binder << created_share.getValueOfId() << "file"
                                       << file.getValueOfId() << now;
                            }
                        }
                    );
                }
            }

            if (!folders.empty()) {
                auto chunks = BatchUtils::Chunk(folders, DEFAULT_BATCH_CHUNK_SIZE);
                for (const auto& chunk : chunks) {
                    std::string insert_sql =
                        "INSERT INTO share_files (share_id, item_type, item_id, created_at) VALUES ";
                    for (size_t i = 0; i < chunk.size(); ++i) {
                        if (i > 0) {
                            insert_sql += ", ";
                        }
                        insert_sql += "($" + std::to_string(i * 4 + 1) + ", $" + std::to_string(i * 4 + 2) + ", $" + std::to_string(i * 4 + 3) + ", $" + std::to_string(i * 4 + 4) + ")";
                    }

                    co_await ExecSqlWithBindings(
                        transaction,
                        insert_sql,
                        [&](auto& binder) {
                            for (const auto& folder : chunk) {
                                binder << created_share.getValueOfId() << "folder"
                                       << folder.getValueOfId() << now;
                            }
                        }
                    );
                }
            }
        } catch (const DrogonDbException& e) {
            Logger::Error(log_context)
                << "Failed to create share (transaction): " << e.base().what();
            if (transaction) {
                try {
                    transaction->rollback();
                } catch (const std::exception& rollback_e) {
                    Logger::Error(log_context)
                        << "Transaction rollback failed: " << rollback_e.what();
                }
            }
            co_return std::unexpected(
                ErrorInfo(ErrorCode::InternalError, "Failed to create share")
            );
        }

        // Commit and release the transaction connection before the fail-open audit insert.
        auto commit_result = co_await disk::file::TransactionRunner::Commit(
            transaction,
            log_context
        );
        if (!commit_result) {
            co_return std::unexpected(ErrorInfo(ErrorCode::InternalError, "Failed to create share"));
        }

        /// 7. 构建响应
        CreateShareResponse response;
        response.share_id = share_code;
        response.share_link = BuildShareLink(share_code);
        if (request.password.has_value() && !request.password->empty()) {
            response.password = *request.password;
        }
        response.permission = SharePermissionToString(request.permission);
        response.expires_at = expires_at.has_value() ? FormatDateTime(*expires_at) : "";
        response.created_at = FormatDateTime(created_share.getValueOfCreatedAt());

        co_await m_audit_service.RecordCreate(ShareCreateAuditEvent{
            .actor_user_id = user_id,
            .share_id = static_cast<uint64_t>(created_share.getValueOfId()),
            .share_code = share_code,
            .file_ids = request.file_ids,
            .folder_ids = request.folder_ids,
            .permission = response.permission,
            .expires_at = response.expires_at.empty() ? std::nullopt : std::optional<std::string>{ response.expires_at },
            .context = audit_context,
            .log_context = log_context,
        });

        Logger::Info(log_context) << "Share created successfully: share_code=" << share_code;
        co_return response;
    }

    auto ShareService::List(
        const ShareListRequest& request,
        uint64_t user_id,
        disk::utils::LogContext log_context
    )
        -> drogon::Task<Result<ShareListResponse>> {
        Logger::Debug(log_context)
            << "Getting share list: user_id=" << user_id << ", status=" << request.status;

        CoroMapper<Shares> mapper(m_db_client);

        /// 构建查询条件
        auto criteria = Criteria(Shares::Cols::_user_id, user_id);

        auto status_filter = GetStatusFilter(request.status);
        if (status_filter.has_value()) {
            criteria = criteria && Criteria(Shares::Cols::_status, *status_filter);
        }

        /// 获取总数
        int total = 0;
        try {
            if (request.status == "active") {
                auto rows = co_await m_db_client->execSqlCoro(
                    "SELECT COUNT(*) AS total FROM shares " "WHERE user_id = $1 AND status = $2 AND (expires_at IS NULL OR expires_at > NOW())",
                    user_id,
                    static_cast<int8_t>(ShareStatus::Active)
                );
                total = rows[0]["total"].as<int>();
            } else {
                total = co_await mapper.count(criteria);
            }
        } catch (const DrogonDbException& e) {
            Logger::Error(log_context) << "Failed to get share count: " << e.base().what();
            co_return std::unexpected(
                ErrorInfo(ErrorCode::InternalError, "Failed to get share list")
            );
        }

        /// 分页查询
        std::vector<Shares> shares;
        try {
            if (request.status == "active") {
                auto rows = co_await m_db_client->execSqlCoro(
                    "SELECT * FROM shares " "WHERE user_id = $1 AND status = $2 AND (expires_at IS NULL OR expires_at > NOW()) " "ORDER BY created_at DESC LIMIT $3 OFFSET $4",
                    user_id,
                    static_cast<int8_t>(ShareStatus::Active),
                    request.page_size,
                    (request.page - 1) * request.page_size
                );
                shares.reserve(rows.size());
                for (const auto& row : rows) {
                    shares.emplace_back(row);
                }
            } else {
                shares = co_await mapper.orderBy(Shares::Cols::_created_at, SortOrder::DESC)
                             .offset((request.page - 1) * request.page_size)
                             .limit(request.page_size)
                             .findBy(criteria);
            }
        } catch (const DrogonDbException& e) {
            Logger::Error(log_context) << "Failed to get share list: " << e.base().what();
            co_return std::unexpected(
                ErrorInfo(ErrorCode::InternalError, "Failed to get share list")
            );
        }

        /// 构建响应
        ShareListResponse response;
        std::vector<uint64_t> share_ids;
        share_ids.reserve(shares.size());
        for (const auto& share : shares) {
            share_ids.push_back(share.getValueOfId());
        }

        auto share_files_map = co_await GetShareFilesBatch(share_ids, log_context);

        for (const auto& share : shares) {
            auto map_it = share_files_map.find(share.getValueOfId());
            const auto& share_files =
                map_it != share_files_map.end() ? map_it->second : std::vector<ShareFile>{};

            ShareItem item;
            item.share_id = share.getValueOfShareCode();
            item.file_name = share_files.empty() ? "" : share_files[0].name;
            item.file_count = static_cast<int>(share_files.size());
            item.share_link = BuildShareLink(share.getValueOfShareCode());
            item.has_password = share.getPasswordHash() != nullptr;
            item.permission = share.getValueOfPermission();
            item.view_count = static_cast<int>(share.getValueOfViewCount());
            item.download_count = static_cast<int>(share.getValueOfDownloadCount());
            item.created_at = FormatDateTime(share.getValueOfCreatedAt());

            if (share.getExpiresAt() != nullptr) {
                item.expires_at = FormatDateTime(*share.getExpiresAt());
            } else {
                item.expires_at = "";
            }

            /// 状态
            auto status_val = share.getValueOfStatus();
            if (status_val == static_cast<int8_t>(ShareStatus::Cancelled)) {
                item.status = "cancelled";
            } else if (status_val == static_cast<int8_t>(ShareStatus::Active)) {
                if (share.getExpiresAt() != nullptr && IsShareExpired(share)) {
                    item.status = "expired";
                } else {
                    item.status = "active";
                }
            } else {
                item.status = "expired";
            }

            response.items.push_back(item);
        }

        /// 分页信息
        response.pagination.page = request.page;
        response.pagination.page_size = request.page_size;
        response.pagination.total = total;
        response.pagination.total_pages =
            request.page_size > 0 ? (total + request.page_size - 1) / request.page_size : 0;

        co_return response;
    }

    auto ShareService::Detail(
        const ShareDetailRequest& request,
        uint64_t user_id,
        disk::utils::LogContext log_context
    )
        -> drogon::Task<Result<ShareDetailResponse>> {
        Logger::Debug(log_context)
            << "Getting share details: share_id=" << request.share_id
            << ", user_id=" << user_id;

        /// 验证分享所有权
        auto share_result =
            co_await ValidateShareOwnership(request.share_id, user_id, log_context);
        if (!share_result) {
            co_return std::unexpected(share_result.error());
        }
        const auto& share = *share_result;

        /// 验证分享状态（必须是有效状态）
        if (!IsShareActive(share)) {
            co_return std::unexpected(
                ErrorInfo(ErrorCode::ShareExpired, "Share cancelled or expired")
            );
        }

        /// 获取分享文件列表
        auto share_files = co_await GetShareFiles(share.getValueOfId(), log_context);

        /// 构建响应
        ShareDetailResponse response;
        response.share_id = share.getValueOfShareCode();
        response.files = share_files;
        response.share_link = BuildShareLink(share.getValueOfShareCode());
        response.has_password = share.getPasswordHash() != nullptr;
        response.permission = share.getValueOfPermission();
        response.view_count = static_cast<int>(share.getValueOfViewCount());
        response.download_count = static_cast<int>(share.getValueOfDownloadCount());
        response.created_at = FormatDateTime(share.getValueOfCreatedAt());

        if (share.getExpiresAt() != nullptr) {
            response.expires_at = FormatDateTime(*share.getExpiresAt());
        } else {
            response.expires_at = "";
        }

        /// 状态
        auto status_val = share.getValueOfStatus();
        if (status_val == static_cast<int8_t>(ShareStatus::Cancelled)) {
            response.status = "cancelled";
        } else if (status_val == static_cast<int8_t>(ShareStatus::Active)) {
            if (share.getExpiresAt() != nullptr && IsShareExpired(share)) {
                response.status = "expired";
            } else {
                response.status = "active";
            }
        } else {
            response.status = "expired";
        }

        co_return response;
    }

    auto ShareService::Update(
        const UpdateShareRequest& request,
        uint64_t user_id,
        disk::utils::LogContext log_context
    )
        -> drogon::Task<Result<UpdateShareResponse>> {
        Logger::Info(log_context)
            << "Updating share settings: share_id=" << request.share_id
            << ", user_id=" << user_id;

        /// 验证分享所有权
        auto share_result =
            co_await ValidateShareOwnership(request.share_id, user_id, log_context);
        if (!share_result) {
            co_return std::unexpected(share_result.error());
        }
        auto share = *share_result;

        /// 验证分享状态（必须是有效状态）
        if (!IsShareActive(share)) {
            co_return std::unexpected(
                ErrorInfo(ErrorCode::ShareExpired, "Share cancelled or expired, cannot update")
            );
        }

        auto now = trantor::Date::now();

        /// 更新过期时间
        if (request.expire_days.has_value()) {
            if (*request.expire_days > 0) {
                share.setExpiresAt(now.after(*request.expire_days * 86400));
            } else {
                share.setExpiresAtToNull();
            }
        }

        /// 更新密码
        if (request.password.has_value()) {
            if (request.password->empty()) {
                share.setPasswordHashToNull();
            } else {
                auto hash_result = utils::HashUtil::HashPassword(*request.password);
                if (!hash_result) {
                    co_return std::unexpected(
                        ErrorInfo(ErrorCode::InternalError, "Password encryption failed")
                    );
                }
                share.setPasswordHash(*hash_result);
            }
        }

        /// 更新权限
        if (request.permission.has_value()) {
            share.setPermission(SharePermissionToString(*request.permission));
        }

        share.setUpdatedAt(now);

        /// 保存更新
        CoroMapper<Shares> mapper(m_db_client);
        try {
            co_await mapper.update(share);
        } catch (const DrogonDbException& e) {
            Logger::Error(log_context) << "Failed to update share: " << e.base().what();
            co_return std::unexpected(
                ErrorInfo(ErrorCode::InternalError, "Failed to update share")
            );
        }

        /// 构建响应
        UpdateShareResponse response;
        response.share_id = request.share_id;
        if (share.getExpiresAt() != nullptr) {
            response.expires_at = FormatDateTime(*share.getExpiresAt());
        } else {
            response.expires_at = "";
        }
        response.has_password = share.getPasswordHash() != nullptr;
        response.permission = share.getValueOfPermission();
        response.updated_at = FormatDateTime(now);

        Logger::Info(log_context)
            << "Share updated successfully: share_id=" << request.share_id;
        co_return response;
    }

    auto ShareService::Cancel(
        const CancelShareRequest& request,
        uint64_t user_id,
        const ShareAuditContext& audit_context,
        disk::utils::LogContext log_context
    )
        -> drogon::Task<Result<CancelShareResponse>> {
        Logger::Info(log_context)
            << "Batch cancel shares: user_id=" << user_id
            << ", share_ids.size()=" << request.share_ids.size();

        CancelShareResponse response;
        response.summary.total = static_cast<int>(request.share_ids.size());
        response.summary.succeeded = 0;
        response.summary.failed = 0;

        if (request.share_ids.empty()) {
            Logger::Info(log_context)
                << "Batch cancel shares completed: succeeded=" << response.summary.succeeded
                << ", failed=" << response.summary.failed;
            co_return response;
        }

        auto chunks = BatchUtils::Chunk(request.share_ids, DEFAULT_BATCH_CHUNK_SIZE);

        for (const auto& chunk : chunks) {
            const auto chunk_result_start = response.results.size();
            auto placeholders = BatchUtils::BuildInPlaceholders(chunk);
            auto select_sql =
                "SELECT id, share_code, user_id, status, updated_at FROM shares WHERE share_code IN (" + placeholders + ")";

            struct ShareRow {
                uint64_t id;
                uint64_t user_id;
                int8_t status;
            };

            std::unordered_map<std::string, ShareRow> share_map;
            bool select_failed = false;
            try {
                auto select_rows = co_await m_db_client->execSqlCoro(select_sql, chunk);
                share_map.reserve(select_rows.size());
                for (const auto& row : select_rows) {
                    auto share_code = row["share_code"].as<std::string>();
                    share_map.emplace(
                        std::move(share_code),
                        ShareRow{ .id = row["id"].as<uint64_t>(),
                                  .user_id = row["user_id"].as<uint64_t>(),
                                  .status = row["status"].as<int8_t>() }
                    );
                }
            } catch (const DrogonDbException& e) {
                Logger::Error(log_context)
                    << "Failed to fetch shares for cancel: " << e.base().what();

                for (const auto& share_id : chunk) {
                    CancelShareResult result;
                    result.share_id = share_id;
                    result.status = "failed";
                    result.error = CancelShareError{ .code = static_cast<int>(ErrorCode::InternalError),
                                                     .message = "Operation failed",
                                                     .reason = "internal_error" };
                    response.results.push_back(result);
                    response.summary.failed++;
                }
                select_failed = true;
            }

            if (select_failed) {
                for (auto result_it = response.results.begin() + static_cast<std::ptrdiff_t>(chunk_result_start);
                     result_it != response.results.end();
                     ++result_it) {
                    co_await m_audit_service.RecordCancel(ShareCancelAuditEvent{
                        .actor_user_id = user_id,
                        .share_id = std::nullopt,
                        .share_code = result_it->share_id,
                        .success = false,
                        .result = "internal_error",
                        .context = audit_context,
                        .log_context = log_context,
                    });
                }
                continue;
            }

            std::unordered_set<std::string> valid_codes_to_cancel;
            valid_codes_to_cancel.reserve(chunk.size());

            for (const auto& share_id : chunk) {
                CancelShareResult result;
                result.share_id = share_id;

                auto map_it = share_map.find(share_id);
                if (map_it == share_map.end()) {
                    result.status = "failed";
                    result.error =
                        CancelShareError{ .code = static_cast<int>(ErrorCode::ShareNotFound),
                                          .message = "Share not found",
                                          .reason = "share_not_found" };
                    response.results.push_back(result);
                    response.summary.failed++;
                    continue;
                }

                const auto& share = map_it->second;

                if (share.user_id != user_id) {
                    result.status = "failed";
                    result.error =
                        CancelShareError{ .code = static_cast<int>(ErrorCode::ShareAccessDenied),
                                          .message = "Access denied",
                                          .reason = "access_denied" };
                    response.results.push_back(result);
                    response.summary.failed++;
                    continue;
                }

                if (share.status == static_cast<int8_t>(ShareStatus::Cancelled) || valid_codes_to_cancel.contains(share_id)) {
                    result.status = "failed";
                    result.error =
                        CancelShareError{ .code = static_cast<int>(ErrorCode::ValidationFailed),
                                          .message = "Share already cancelled",
                                          .reason = "already_cancelled" };
                    response.results.push_back(result);
                    response.summary.failed++;
                    continue;
                }

                valid_codes_to_cancel.insert(share_id);
                result.status = "success";
                response.summary.succeeded++;
                response.results.push_back(result);
            }

            if (!valid_codes_to_cancel.empty()) {
                std::vector<std::string> valid_share_codes(
                    valid_codes_to_cancel.begin(),
                    valid_codes_to_cancel.end()
                );

                auto update_placeholders = BatchUtils::BuildInPlaceholders(valid_share_codes, 2);
                auto update_sql =
                    "UPDATE shares SET status = 0, updated_at = $1 WHERE share_code IN (" + update_placeholders + ")";
                std::vector<std::string> update_args;
                update_args.reserve(valid_share_codes.size() + 1);
                update_args.push_back(trantor::Date::now().toDbStringLocal());
                update_args.insert(
                    update_args.end(),
                    valid_share_codes.begin(),
                    valid_share_codes.end()
                );

                try {
                    co_await m_db_client->execSqlCoro(update_sql, std::as_const(update_args));
                } catch (const DrogonDbException& e) {
                    Logger::Error(log_context)
                        << "Failed to cancel share: " << e.base().what();

                    for (auto result_it = response.results.begin() + static_cast<std::ptrdiff_t>(chunk_result_start);
                         result_it != response.results.end();
                         ++result_it) {
                        auto& result = *result_it;
                        if (result.status == "success" && valid_codes_to_cancel.contains(result.share_id) && result.error == std::nullopt) {
                            result.status = "failed";
                            result.error = CancelShareError{ .code = static_cast<int>(ErrorCode::InternalError),
                                                             .message = "Operation failed",
                                                             .reason = "internal_error" };
                            response.summary.succeeded--;
                            response.summary.failed++;
                        }
                    }
                }
            }

            for (auto result_it = response.results.begin() + static_cast<std::ptrdiff_t>(chunk_result_start);
                 result_it != response.results.end();
                 ++result_it) {
                const auto share_it = share_map.find(result_it->share_id);
                const auto internal_share_id = share_it == share_map.end() ? std::optional<uint64_t>{} : std::optional<uint64_t>{ share_it->second.id };
                co_await m_audit_service.RecordCancel(ShareCancelAuditEvent{
                    .actor_user_id = user_id,
                    .share_id = internal_share_id,
                    .share_code = result_it->share_id,
                    .success = result_it->status == "success",
                    .result = result_it->error.has_value() ? result_it->error->reason : std::string("success"),
                    .context = audit_context,
                    .log_context = log_context,
                });
            }
        }

        Logger::Info(log_context)
            << "Batch cancel shares completed: succeeded=" << response.summary.succeeded
            << ", failed=" << response.summary.failed;

        co_return response;
    }

    auto ShareService::Access(
        const AccessShareRequest& request,
        const ShareAuditContext& audit_context,
        disk::utils::LogContext log_context
    )
        -> drogon::Task<Result<AccessShareResponse>> {
        Logger::Info(log_context) << "Verifying share access: share_id=" << request.share_id;

        /// 查找分享。公开访问不区分不存在的分享和密码验证失败。
        auto share_result = co_await FindShareByCode(request.share_id, log_context);
        if (!share_result) {
            if (share_result.error().code == ErrorCode::ShareNotFound) {
                auto failure = co_await HandleFailedShareAccess(
                    request.share_id,
                    audit_context.ip_address,
                    log_context
                );
                co_await RecordFailedShareAccess(
                    std::nullopt,
                    request.share_id,
                    failure,
                    audit_context,
                    log_context
                );
                co_return std::unexpected(failure.error);
            }
            co_return std::unexpected(share_result.error());
        }
        const auto& share = *share_result;

        /// 验证分享状态
        if (share.getValueOfStatus() != static_cast<int8_t>(ShareStatus::Active)) {
            co_await m_audit_service.RecordAccess(ShareAccessAuditEvent{
                .share_id = share.getValueOfId(),
                .share_code = request.share_id,
                .success = false,
                .result = "cancelled",
                .context = audit_context,
                .log_context = log_context,
            });
            co_return std::unexpected(
                ErrorInfo(ErrorCode::ShareExpired, "Share has been cancelled")
            );
        }

        /// 验证是否过期
        if (share.getExpiresAt() != nullptr && IsShareExpired(share)) {
            co_await m_audit_service.RecordAccess(ShareAccessAuditEvent{
                .share_id = share.getValueOfId(),
                .share_code = request.share_id,
                .success = false,
                .result = "expired",
                .context = audit_context,
                .log_context = log_context,
            });
            co_return std::unexpected(ErrorInfo(ErrorCode::ShareExpired, "Share has expired"));
        }

        /// 仅记录失败的密码验证；正确密码不占用失败额度。
        if (share.getPasswordHash() != nullptr) {
            if (!request.password.has_value() || request.password->empty() ||
                !VerifyPassword(share, *request.password)) {
                auto failure = co_await HandleFailedShareAccess(
                    request.share_id,
                    audit_context.ip_address,
                    log_context
                );
                co_await RecordFailedShareAccess(
                    share.getValueOfId(),
                    request.share_id,
                    failure,
                    audit_context,
                    log_context
                );
                co_return std::unexpected(failure.error);
            }
        }
        auto token_result = services::TokenService::GenerateShareToken(
            m_jwt_secret,
            share.getValueOfShareCode(),
            share.getValueOfId(),
            share.getValueOfPermission(),
            log_context
        );
        if (!token_result) {
            co_await m_audit_service.RecordAccess(ShareAccessAuditEvent{
                .share_id = share.getValueOfId(),
                .share_code = request.share_id,
                .success = false,
                .result = "token_generation_failed",
                .context = audit_context,
                .log_context = log_context,
            });
            co_return std::unexpected(token_result.error());
        }

        /// 增加访问次数
        co_await IncrementViewCount(share.getValueOfId(), log_context);

        /// 获取分享文件列表
        auto share_files = co_await GetShareFiles(share.getValueOfId(), log_context);

        /// 构建响应
        AccessShareResponse response;
        response.share_token = *token_result;
        response.expires_in = services::TokenService::GetShareTokenExpireSeconds();
        response.permission = share.getValueOfPermission();
        response.files = share_files;

        co_await m_audit_service.RecordAccess(ShareAccessAuditEvent{
            .share_id = share.getValueOfId(),
            .share_code = request.share_id,
            .success = true,
            .result = "success",
            .context = audit_context,
            .log_context = log_context,
        });

        Logger::Info(log_context)
            << "Share access verified successfully: share_id=" << request.share_id;
        co_return response;
    }

    auto ShareService::Browse(
        const BrowseShareRequest& request,
        uint64_t share_id,
        disk::utils::LogContext log_context
    )
        -> drogon::Task<Result<BrowseShareResponse>> {
        Logger::Debug(log_context)
            << "Browsing share content: share_id=" << request.share_id
            << ", internal_share_id=" << share_id;

        auto active_result = co_await ValidateShareActive(share_id, log_context);
        if (!active_result) {
            co_return std::unexpected(active_result.error());
        }

        BrowseShareResponse response;
        BrowseBreadcrumb root;
        root.id = 0;
        root.name = "root";
        response.breadcrumb.push_back(root);

        if (!request.folder_id.has_value() || *request.folder_id == 0) {
            auto share_files = co_await GetShareFiles(share_id, log_context);
            for (const auto& file : share_files) {
                BrowseItem item;
                item.id = file.id;
                item.name = file.name;
                item.type = file.type;
                item.size = file.size;
                item.file_hash = file.file_hash;
                item.supports_range = file.supports_range;
                item.item_count = file.item_count;
                response.items.push_back(item);
            }
            co_return response;
        }

        auto folder_id = *request.folder_id;
        try {
            auto folder_rows = co_await m_db_client->execSqlCoro(
                "SELECT fo.id, fo.name, fo.path, fo.depth, fo.user_id " "FROM folders fo WHERE fo.id = $1 AND " + BuildSharedFolderAccessPredicate("fo", 2),
                folder_id,
                share_id
            );
            if (folder_rows.empty()) {
                co_return std::unexpected(
                    ErrorInfo(ErrorCode::ShareAccessDenied, "Folder is not in share")
                );
            }

            const auto current_path = folder_rows[0]["path"].as<std::string>();
            const auto folder_user_id = folder_rows[0]["user_id"].as<uint64_t>();

            auto file_rows = co_await m_db_client->execSqlCoro(
                "SELECT f.id, f.name, f.size, fc.hash_md5, 'file' AS type " "FROM files f " "LEFT JOIN file_contents fc ON f.content_id = fc.id " "WHERE f.folder_id = $1 AND f.user_id = $2 ORDER BY f.name, f.id",
                folder_id,
                folder_user_id
            );
            for (const auto& row : file_rows) {
                BrowseItem item;
                item.id = row["id"].as<uint64_t>();
                item.name = row["name"].as<std::string>();
                item.type = row["type"].as<std::string>();
                item.size = row["size"].as<uint64_t>();
                item.file_hash = row["hash_md5"].isNull() ? "" : row["hash_md5"].as<std::string>();
                item.supports_range = true;
                response.items.push_back(item);
            }

            auto child_folder_rows = co_await m_db_client->execSqlCoro(
                "SELECT fo.id, fo.name, fo.item_count, 0 AS size, 'folder' AS type " "FROM folders fo WHERE fo.parent_id = $1 AND fo.user_id = $2 ORDER BY fo.name, fo.id",
                folder_id,
                folder_user_id
            );
            for (const auto& row : child_folder_rows) {
                BrowseItem item;
                item.id = row["id"].as<uint64_t>();
                item.name = row["name"].as<std::string>();
                item.type = row["type"].as<std::string>();
                item.size = row["size"].as<uint64_t>();
                item.item_count = row["item_count"].as<uint32_t>();
                response.items.push_back(item);
            }

            auto breadcrumb_rows = co_await m_db_client->execSqlCoro(
                "SELECT id, name FROM folders fo " "WHERE $1 LIKE CONCAT(fo.path, '%') AND fo.path <> '/' AND " +
                    BuildSharedFolderAccessPredicate("fo", 2) + " ORDER BY depth, id",
                current_path,
                share_id
            );
            for (const auto& row : breadcrumb_rows) {
                BrowseBreadcrumb crumb;
                crumb.id = row["id"].as<uint64_t>();
                crumb.name = row["name"].as<std::string>();
                response.breadcrumb.push_back(crumb);
            }
        } catch (const DrogonDbException& e) {
            Logger::Error(log_context)
                << "Failed to browse share folder: " << e.base().what();
            co_return std::unexpected(
                ErrorInfo(ErrorCode::InternalError, "Failed to browse share content")
            );
        }

        co_return response;
    }

    auto ShareService::GetDownloadInfo(
        const DownloadShareRequest& request,
        uint64_t share_id,
        disk::utils::LogContext log_context
    )
        -> drogon::Task<Result<DownloadInfo>> {
        Logger::Debug(log_context)
            << "Getting download info: share_id=" << request.share_id
            << ", file_id=" << request.file_id;

        /// 单次 4 表 JOIN 查询：shares + share_files + files + file_contents
        try {
            auto rows = co_await m_db_client->execSqlCoro(
                "SELECT s.id AS share_id, s.permission, s.status, " "(s.expires_at IS NOT NULL AND s.expires_at <= NOW()) AS is_expired, " "f.id AS file_id, f.name AS file_name, f.size AS file_size, f.content_id, " "fc.id AS content_id, fc.hash_md5, fc.size AS content_size, fc.storage_path, fc.mime_type " "FROM shares s " "LEFT JOIN files f ON f.id = $1 AND " + BuildSharedFileAccessPredicate("f", 2) + " " "LEFT JOIN file_contents fc ON f.content_id = fc.id " "WHERE s.id = $4",
                request.file_id,
                share_id,
                share_id,
                share_id
            );

            if (rows.empty()) {
                co_return std::unexpected(ErrorInfo(ErrorCode::ShareNotFound, "Share not found"));
            }

            const auto& row = rows[0];
            auto status = row["status"].as<int>();
            auto is_expired = row["is_expired"].as<bool>();
            if (status != static_cast<int>(ShareStatus::Active) || is_expired) {
                co_return std::unexpected(ErrorInfo(ErrorCode::ShareExpired, "Share is not active"));
            }
            if (row["file_id"].isNull()) {
                co_return std::unexpected(ErrorInfo(ErrorCode::FileNotFound, "File not in share"));
            }

            auto permission = row["permission"].as<std::string>();

            if (permission != "download") {
                Logger::Warn(log_context)
                    << "Insufficient share permissions: share_id=" << share_id
                    << ", permission=" << permission;
                co_return std::unexpected(
                    ErrorInfo(ErrorCode::ShareAccessDenied, "Share is view-only, download not allowed")
                );
            }

            DownloadInfo info;
            info.file_id = row["file_id"].as<uint64_t>();
            info.filename = row["file_name"].as<std::string>();
            info.file_size = row["file_size"].as<uint64_t>();
            info.mime_type =
                row["mime_type"].isNull() ? "application/octet-stream" : row["mime_type"].as<std::string>();
            info.file_hash = row["hash_md5"].as<std::string>();
            info.blob = disk::storage::BlobDescriptor{
                .content_id = row["content_id"].as<uint64_t>(),
                .hash_md5 = info.file_hash,
                .storage_path = row["storage_path"].as<std::string>(),
                .size = row["content_size"].as<uint64_t>()
            };
            info.supports_range = true;

            co_return info;
        } catch (const DrogonDbException& e) {
            Logger::Error(log_context)
                << "Failed to get download info: " << e.base().what();
            co_return std::unexpected(
                ErrorInfo(ErrorCode::InternalError, "Failed to get download info")
            );
        }
    }

    auto ShareService::CompleteDownload(
        const DownloadShareRequest& request,
        uint64_t share_id,
        const ShareDownloadOutcome& outcome,
        const ShareAuditContext& audit_context,
        disk::utils::LogContext log_context
    ) -> drogon::Task<void> {
        if (outcome.update_statistics) {
            if (outcome.success) {
                co_await UpdateFileDownloadMetadata(request.file_id, log_context);
            }
            co_await IncrementDownloadCount(share_id, log_context);
        }
        co_await m_audit_service.RecordDownload(ShareDownloadAuditEvent{
            .share_id = share_id,
            .share_code = request.share_id,
            .file_id = request.file_id,
            .bytes = outcome.bytes,
            .http_status = outcome.http_status,
            .success = outcome.success,
            .result = outcome.result,
            .context = audit_context,
            .log_context = log_context,
        });
    }

    auto ShareService::SaveToDrive(
        const SaveShareItemsRequest& request,
        uint64_t share_id,
        uint64_t target_user_id,
        disk::utils::LogContext log_context
    ) -> drogon::Task<Result<SaveShareItemsResponse>> {
        Logger::Debug(log_context)
            << "Saving share items: internal_share_id=" << share_id
            << ", target_user_id=" << target_user_id;

        std::shared_ptr<drogon::orm::Transaction> transaction;
        try {
            auto share_rows = co_await m_db_client->execSqlCoro(
                "SELECT permission, status, " "(expires_at IS NOT NULL AND expires_at <= NOW()) AS is_expired " "FROM shares WHERE id = $1",
                share_id
            );
            if (share_rows.empty()) {
                co_return std::unexpected(ErrorInfo(ErrorCode::ShareNotFound, "Share not found"));
            }
            auto permission = share_rows[0]["permission"].as<std::string>();
            auto status = share_rows[0]["status"].as<int>();
            auto is_expired = share_rows[0]["is_expired"].as<bool>();
            if (status != static_cast<int>(ShareStatus::Active) || is_expired) {
                co_return std::unexpected(ErrorInfo(ErrorCode::ShareExpired, "Share is not active"));
            }
            if (permission != "download") {
                co_return std::unexpected(
                    ErrorInfo(ErrorCode::ShareAccessDenied, "Share is view-only, save not allowed")
                );
            }

            auto target_path = std::string("/");
            auto target_depth = uint32_t{ 0 };
            if (request.target_folder_id > 0) {
                auto target_rows = co_await m_db_client->execSqlCoro(
                    "SELECT path, depth FROM folders WHERE id = $1 AND user_id = $2",
                    request.target_folder_id,
                    target_user_id
                );
                if (target_rows.empty()) {
                    co_return std::unexpected(
                        ErrorInfo(ErrorCode::FolderNotFound, "Target folder not found")
                    );
                }
                target_path = target_rows[0]["path"].as<std::string>();
                target_depth = target_rows[0]["depth"].as<uint32_t>();
            }

            std::vector<Files> files_to_save;
            for (auto file_id : request.file_ids) {
                auto rows = co_await m_db_client->execSqlCoro(
                    "SELECT f.* FROM files f WHERE f.id = $1 AND " + BuildSharedFileAccessPredicate("f", 2),
                    file_id,
                    share_id,
                    share_id
                );
                if (rows.empty()) {
                    co_return std::unexpected(
                        ErrorInfo(ErrorCode::ShareAccessDenied, "File is not in share")
                    );
                }
                files_to_save.emplace_back(rows[0], -1);
            }

            struct FolderPlan {
                Folders root;
                std::vector<Folders> folders;
                std::vector<Files> files;
            };

            std::vector<FolderPlan> folder_plans;
            for (auto folder_id : request.folder_ids) {
                auto rows = co_await m_db_client->execSqlCoro(
                    "SELECT fo.* FROM folders fo WHERE fo.id = $1 AND " + BuildSharedFolderAccessPredicate("fo", 2),
                    folder_id,
                    share_id
                );
                if (rows.empty()) {
                    co_return std::unexpected(
                        ErrorInfo(ErrorCode::ShareAccessDenied, "Folder is not in share")
                    );
                }

                FolderPlan plan;
                plan.root = Folders(rows[0], -1);
                auto root_path = plan.root.getValueOfPath();
                auto root_user_id = plan.root.getValueOfUserId();
                auto folder_rows = co_await m_db_client->execSqlCoro(
                    "SELECT * FROM folders WHERE user_id = $1 AND path LIKE CONCAT($2, '%') ORDER BY depth, id",
                    root_user_id,
                    root_path
                );
                for (const auto& row : folder_rows) {
                    plan.folders.emplace_back(row, -1);
                }
                auto file_rows = co_await m_db_client->execSqlCoro(
                    "SELECT f.* FROM files f " "JOIN folders fo ON f.folder_id = fo.id " "WHERE f.user_id = $1 AND fo.user_id = $2 AND fo.path LIKE CONCAT($3, '%') ORDER BY fo.depth, f.id",
                    root_user_id,
                    root_user_id,
                    root_path
                );
                for (const auto& row : file_rows) {
                    plan.files.emplace_back(row, -1);
                }
                folder_plans.push_back(std::move(plan));
            }

            uint64_t total_size = 0;
            for (const auto& file : files_to_save) {
                total_size += file.getValueOfSize();
            }
            for (const auto& plan : folder_plans) {
                for (const auto& file : plan.files) {
                    total_size += file.getValueOfSize();
                }
            }

            transaction = co_await disk::file::TransactionRunner::Begin(m_db_client);
            if (total_size > 0) {
                auto quota_result = co_await transaction->execSqlCoro(
                    "UPDATE users SET storage_used = storage_used + $1 " "WHERE id = $2 AND storage_used + $3 <= storage_quota",
                    total_size,
                    target_user_id,
                    total_size
                );
                if (quota_result.affectedRows() == 0) {
                    transaction->rollback();
                    co_return std::unexpected(ErrorInfo(ErrorCode::StorageQuotaExceeded));
                }
            }

            disk::content::ContentService content_service(m_db_client);
            SaveShareItemsResponse response;
            uint64_t actual_size = 0;
            int saved_top_level_count = 0;

            {
                CoroMapper<Files> file_mapper(transaction);
                CoroMapper<Folders> folder_mapper(transaction);

                for (const auto& source_file : files_to_save) {
                    auto conflict_rows = co_await transaction->execSqlCoro(
                        "SELECT id FROM files WHERE user_id = $1 AND folder_id = $2 AND name = $3 LIMIT 1",
                        target_user_id,
                        request.target_folder_id,
                        source_file.getValueOfName()
                    );
                    if (!conflict_rows.empty()) {
                        continue;
                    }

                    if (source_file.getContentId()) {
                        auto increment_result = co_await content_service.IncrementRefCount(
                            transaction,
                            *source_file.getContentId(),
                            1,
                            log_context
                        );
                        if (!increment_result) {
                            throw std::runtime_error(increment_result.error().message);
                        }
                    }

                    Files copied_file;
                    copied_file.setUserId(target_user_id);
                    if (source_file.getContentId()) {
                        copied_file.setContentId(*source_file.getContentId());
                    }
                    copied_file.setFolderId(request.target_folder_id);
                    copied_file.setName(source_file.getValueOfName());
                    copied_file.setExtension(source_file.getValueOfExtension());
                    copied_file.setSize(source_file.getValueOfSize());
                    copied_file.setMimeType(source_file.getValueOfMimeType());
                    copied_file.setPath(BuildFilePath(target_path, source_file.getValueOfName()));
                    copied_file.setIsFavorite(0);
                    copied_file.setDownloadCount(0);
                    copied_file.setCreatedAt(trantor::Date::now());
                    copied_file.setUpdatedAt(trantor::Date::now());
                    co_await file_mapper.insert(copied_file);
                    ++response.saved_file_count;
                    ++saved_top_level_count;
                    actual_size += source_file.getValueOfSize();
                }

                for (const auto& plan : folder_plans) {
                    auto conflict_rows = co_await transaction->execSqlCoro(
                        "SELECT id FROM folders WHERE user_id = $1 AND parent_id = $2 AND name = $3 LIMIT 1",
                        target_user_id,
                        request.target_folder_id,
                        plan.root.getValueOfName()
                    );
                    if (!conflict_rows.empty()) {
                        continue;
                    }

                    std::unordered_map<uint64_t, uint64_t> folder_id_map;
                    std::unordered_map<uint64_t, std::string> folder_path_map;
                    auto root_path = BuildFolderPath(target_path, plan.root.getValueOfName());
                    auto root_depth = target_depth + 1;

                    Folders root_folder;
                    root_folder.setUserId(target_user_id);
                    root_folder.setParentId(request.target_folder_id);
                    root_folder.setName(plan.root.getValueOfName());
                    root_folder.setPath(root_path);
                    root_folder.setDepth(root_depth);
                    root_folder.setItemCount(plan.root.getValueOfItemCount());
                    root_folder.setCreatedAt(trantor::Date::now());
                    root_folder.setUpdatedAt(trantor::Date::now());
                    auto inserted_root = co_await folder_mapper.insert(root_folder);
                    folder_id_map[plan.root.getValueOfId()] = inserted_root.getValueOfId();
                    folder_path_map[plan.root.getValueOfId()] = root_path;
                    ++response.saved_folder_count;
                    ++saved_top_level_count;

                    for (const auto& folder : plan.folders) {
                        if (folder.getValueOfId() == plan.root.getValueOfId()) {
                            continue;
                        }
                        auto parent_it = folder_id_map.find(folder.getValueOfParentId());
                        auto parent_path_it = folder_path_map.find(folder.getValueOfParentId());
                        if (parent_it == folder_id_map.end() || parent_path_it == folder_path_map.end()) {
                            continue;
                        }

                        auto folder_path = BuildFolderPath(parent_path_it->second, folder.getValueOfName());
                        auto depth_delta = folder.getValueOfDepth() > plan.root.getValueOfDepth() ? folder.getValueOfDepth() - plan.root.getValueOfDepth() : 1;

                        Folders copied_folder;
                        copied_folder.setUserId(target_user_id);
                        copied_folder.setParentId(parent_it->second);
                        copied_folder.setName(folder.getValueOfName());
                        copied_folder.setPath(folder_path);
                        copied_folder.setDepth(root_depth + depth_delta);
                        copied_folder.setItemCount(folder.getValueOfItemCount());
                        copied_folder.setCreatedAt(trantor::Date::now());
                        copied_folder.setUpdatedAt(trantor::Date::now());
                        auto inserted_folder = co_await folder_mapper.insert(copied_folder);
                        folder_id_map[folder.getValueOfId()] = inserted_folder.getValueOfId();
                        folder_path_map[folder.getValueOfId()] = folder_path;
                        ++response.saved_folder_count;
                    }

                    for (const auto& source_file : plan.files) {
                        auto folder_it = folder_id_map.find(source_file.getValueOfFolderId());
                        auto path_it = folder_path_map.find(source_file.getValueOfFolderId());
                        if (folder_it == folder_id_map.end() || path_it == folder_path_map.end()) {
                            continue;
                        }

                        if (source_file.getContentId()) {
                            auto increment_result = co_await content_service.IncrementRefCount(
                                transaction,
                                *source_file.getContentId(),
                                1,
                                log_context
                            );
                            if (!increment_result) {
                                throw std::runtime_error(increment_result.error().message);
                            }
                        }

                        Files copied_file;
                        copied_file.setUserId(target_user_id);
                        if (source_file.getContentId()) {
                            copied_file.setContentId(*source_file.getContentId());
                        }
                        copied_file.setFolderId(folder_it->second);
                        copied_file.setName(source_file.getValueOfName());
                        copied_file.setExtension(source_file.getValueOfExtension());
                        copied_file.setSize(source_file.getValueOfSize());
                        copied_file.setMimeType(source_file.getValueOfMimeType());
                        copied_file.setPath(BuildFilePath(path_it->second, source_file.getValueOfName()));
                        copied_file.setIsFavorite(0);
                        copied_file.setDownloadCount(0);
                        copied_file.setCreatedAt(trantor::Date::now());
                        copied_file.setUpdatedAt(trantor::Date::now());
                        co_await file_mapper.insert(copied_file);
                        ++response.saved_file_count;
                        actual_size += source_file.getValueOfSize();
                    }
                }
            }

            response.saved_count = response.saved_file_count + response.saved_folder_count;

            if (request.target_folder_id > 0 && saved_top_level_count > 0) {
                co_await transaction->execSqlCoro(
                    "UPDATE folders SET item_count = item_count + $1, updated_at = $2 WHERE id = $3 AND user_id = $4",
                    saved_top_level_count,
                    trantor::Date::now(),
                    request.target_folder_id,
                    target_user_id
                );
            }

            auto reserved_size = static_cast<int64_t>(total_size);
            auto consumed_size = static_cast<int64_t>(actual_size);
            if (reserved_size > consumed_size) {
                co_await transaction->execSqlCoro(
                    "UPDATE users SET storage_used = storage_used - $1 WHERE id = $2",
                    reserved_size - consumed_size,
                    target_user_id
                );
            }

            auto save_commit_result = co_await disk::file::TransactionRunner::Commit(
                transaction,
                log_context
            );
            if (!save_commit_result) {
                co_return std::unexpected(
                    ErrorInfo(ErrorCode::InternalError, "Failed to save share items")
                );
            }
            if (response.saved_count > 0) {
                co_await disk::file::FileListCache::Invalidate(
                    m_redis_service,
                    target_user_id,
                    log_context
                );
            }

            co_return response;
        } catch (const DrogonDbException& e) {
            Logger::Error(log_context)
                << "Failed to save share items: " << e.base().what();
            if (transaction) {
                try {
                    transaction->rollback();
                } catch (const std::exception& rollback_e) {
                    Logger::Error(log_context)
                        << "Transaction rollback failed: " << rollback_e.what();
                }
            }
            co_return std::unexpected(
                ErrorInfo(ErrorCode::InternalError, "Failed to save share items")
            );
        } catch (const std::exception& e) {
            Logger::Error(log_context) << "Failed to save share items: " << e.what();
            if (transaction) {
                try {
                    transaction->rollback();
                } catch (const std::exception& rollback_e) {
                    Logger::Error(log_context)
                        << "Transaction rollback failed: " << rollback_e.what();
                }
            }
            co_return std::unexpected(
                ErrorInfo(ErrorCode::InternalError, "Failed to save share items")
            );
        }
    }

    auto ShareService::FindShareByCode(
        const std::string& share_code,
        disk::utils::LogContext log_context
    ) const
        -> drogon::Task<Result<Shares>> {
        CoroMapper<Shares> mapper(m_db_client);

        try {
            auto share = co_await mapper.findOne(Criteria(Shares::Cols::_share_code, share_code));
            co_return share;
        } catch (const UnexpectedRows&) {
            co_return std::unexpected(ErrorInfo(ErrorCode::ShareNotFound, "Share not found"));
        } catch (const DrogonDbException& e) {
            Logger::Error(log_context) << "Failed to find share: " << e.base().what();
            co_return std::unexpected(ErrorInfo(ErrorCode::InternalError, "Failed to find share"));
        }
    }

    /// ==================== 私有方法 ====================

    auto ShareService::GenerateShareCode() -> std::string {
        constexpr const char* chars =
            "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
        constexpr int code_length = 8;

        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dis(0, static_cast<int>(strlen(chars)) - 1);

        std::string code;
        code.reserve(code_length);

        for (int i = 0; i < code_length; ++i) {
            code += chars[dis(gen)];
        }

        return code;
    }

    auto ShareService::ValidateFileOwnership(
        const std::vector<uint64_t>& file_ids,
        uint64_t user_id,
        disk::utils::LogContext log_context
    ) const -> drogon::Task<Result<std::vector<Files>>> {
        if (file_ids.empty()) {
            co_return std::unexpected(
                ErrorInfo(ErrorCode::InvalidParameter, "File ID list cannot be empty")
            );
        }

        auto unique_file_ids = file_ids;
        std::sort(unique_file_ids.begin(), unique_file_ids.end());
        unique_file_ids.erase(
            std::unique(unique_file_ids.begin(), unique_file_ids.end()),
            unique_file_ids.end()
        );

        auto in_clause = BatchUtils::BuildSafeNumericInClause(unique_file_ids);

        std::vector<Files> files;
        files.reserve(file_ids.size());

        try {
            auto sql =
                "SELECT f.* FROM files f WHERE f.user_id = $1 AND f.id IN (" + in_clause + ")";
            auto result = co_await m_db_client->execSqlCoro(sql, user_id);

            std::vector<Files> matched_files;
            matched_files.reserve(result.size());
            std::unordered_map<uint64_t, size_t> file_index_by_id;
            file_index_by_id.reserve(result.size());
            for (const auto& row : result) {
                auto file = Files(row);
                auto file_id = file.getValueOfId();
                matched_files.push_back(std::move(file));
                file_index_by_id.emplace(file_id, matched_files.size() - 1);
            }

            for (auto file_id : file_ids) {
                auto it = file_index_by_id.find(file_id);
                if (it == file_index_by_id.end()) {
                    Logger::Warn(log_context)
                        << "File not found or not owned by user: file_id=" << file_id;
                    co_return std::unexpected(
                        ErrorInfo(ErrorCode::FileNotFound, "File not found or no permission")
                    );
                }
                files.push_back(matched_files[it->second]);
            }
        } catch (const DrogonDbException& e) {
            Logger::Error(log_context)
                << "Failed to validate file ownership: " << e.base().what();
            co_return std::unexpected(
                ErrorInfo(ErrorCode::InternalError, "Failed to validate file ownership")
            );
        }

        co_return files;
    }

    auto ShareService::ValidateFolderOwnership(
        const std::vector<uint64_t>& folder_ids,
        uint64_t user_id,
        disk::utils::LogContext log_context
    ) const -> drogon::Task<Result<std::vector<Folders>>> {
        if (folder_ids.empty()) {
            co_return std::unexpected(
                ErrorInfo(ErrorCode::InvalidParameter, "Folder ID list cannot be empty")
            );
        }

        auto unique_folder_ids = folder_ids;
        std::sort(unique_folder_ids.begin(), unique_folder_ids.end());
        unique_folder_ids.erase(
            std::unique(unique_folder_ids.begin(), unique_folder_ids.end()),
            unique_folder_ids.end()
        );

        auto in_clause = BatchUtils::BuildSafeNumericInClause(unique_folder_ids);

        std::vector<Folders> folders;
        folders.reserve(folder_ids.size());

        try {
            auto sql =
                "SELECT fo.* FROM folders fo WHERE fo.user_id = $1 AND fo.id IN (" + in_clause + ")";
            auto result = co_await m_db_client->execSqlCoro(sql, user_id);

            std::vector<Folders> matched_folders;
            matched_folders.reserve(result.size());
            std::unordered_map<uint64_t, size_t> folder_index_by_id;
            folder_index_by_id.reserve(result.size());
            for (const auto& row : result) {
                auto folder = Folders(row);
                auto folder_id = folder.getValueOfId();
                matched_folders.push_back(std::move(folder));
                folder_index_by_id.emplace(folder_id, matched_folders.size() - 1);
            }

            for (auto folder_id : folder_ids) {
                auto it = folder_index_by_id.find(folder_id);
                if (it == folder_index_by_id.end()) {
                    Logger::Warn(log_context)
                        << "Folder not found or not owned by user: folder_id=" << folder_id;
                    co_return std::unexpected(
                        ErrorInfo(ErrorCode::FolderNotFound, "Folder not found or no permission")
                    );
                }
                folders.push_back(matched_folders[it->second]);
            }
        } catch (const DrogonDbException& e) {
            Logger::Error(log_context)
                << "Failed to validate folder ownership: " << e.base().what();
            co_return std::unexpected(
                ErrorInfo(ErrorCode::InternalError, "Failed to validate folder ownership")
            );
        }

        co_return folders;
    }

    auto ShareService::ValidateShareOwnership(
        const std::string& share_code,
        uint64_t user_id,
        disk::utils::LogContext log_context
    ) const
        -> drogon::Task<Result<Shares>> {
        auto share_result = co_await FindShareByCode(share_code, log_context);
        if (!share_result) {
            co_return std::unexpected(share_result.error());
        }

        if (share_result->getValueOfUserId() != user_id) {
            co_return std::unexpected(ErrorInfo(ErrorCode::ShareAccessDenied, "Access denied"));
        }

        co_return *share_result;
    }

    auto ShareService::GetShareFiles(
        uint64_t share_id,
        disk::utils::LogContext log_context
    ) const
        -> drogon::Task<std::vector<ShareFile>> {
        std::vector<ShareFile> result;

        struct OrderedShareFile {
            uint64_t share_file_id;
            ShareFile share_file;
        };

        try {
            auto file_rows = co_await m_db_client->execSqlCoro(
                "SELECT sf.id AS share_file_id, f.id, f.name, f.size, fc.hash_md5, 'file' AS type " "FROM share_files sf " "JOIN files f ON sf.item_id = f.id " "LEFT JOIN file_contents fc ON f.content_id = fc.id " "WHERE sf.share_id = $1 AND sf.item_type = 'file' " "ORDER BY sf.id",
                share_id
            );

            auto folder_rows = co_await m_db_client->execSqlCoro(
                "SELECT sf.id AS share_file_id, fo.id, fo.name, 0 AS size, fo.item_count, 'folder' AS type " "FROM share_files sf " "JOIN folders fo ON sf.item_id = fo.id " "WHERE sf.share_id = $1 AND sf.item_type = 'folder' " "ORDER BY sf.id",
                share_id
            );

            std::vector<OrderedShareFile> ordered_items;
            ordered_items.reserve(file_rows.size() + folder_rows.size());

            for (const auto& row : file_rows) {
                ShareFile sf_item;
                sf_item.id = row["id"].as<uint64_t>();
                sf_item.name = row["name"].as<std::string>();
                sf_item.type = row["type"].as<std::string>();
                sf_item.size = row["size"].as<uint64_t>();
                sf_item.file_hash = row["hash_md5"].isNull() ? "" : row["hash_md5"].as<std::string>();
                sf_item.supports_range = true;
                ordered_items.push_back(
                    OrderedShareFile{ row["share_file_id"].as<uint64_t>(), std::move(sf_item) }
                );
            }

            for (const auto& row : folder_rows) {
                ShareFile sf_item;
                sf_item.id = row["id"].as<uint64_t>();
                sf_item.name = row["name"].as<std::string>();
                sf_item.type = row["type"].as<std::string>();
                sf_item.size = row["size"].as<uint64_t>();
                sf_item.item_count = row["item_count"].as<uint32_t>();
                ordered_items.push_back(
                    OrderedShareFile{ row["share_file_id"].as<uint64_t>(), std::move(sf_item) }
                );
            }

            std::sort(
                ordered_items.begin(),
                ordered_items.end(),
                [](const OrderedShareFile& lhs, const OrderedShareFile& rhs) {
                    return lhs.share_file_id < rhs.share_file_id;
                }
            );

            result.reserve(ordered_items.size());
            for (auto& item : ordered_items) {
                result.push_back(std::move(item.share_file));
            }
        } catch (const DrogonDbException& e) {
            Logger::Error(log_context)
                << "Failed to get share file list: " << e.base().what();
        }

        co_return result;
    }

    auto ShareService::GetShareFilesBatch(
        const std::vector<uint64_t>& share_ids,
        disk::utils::LogContext log_context
    ) const
        -> drogon::Task<std::unordered_map<uint64_t, std::vector<ShareFile>>> {
        std::unordered_map<uint64_t, std::vector<ShareFile>> result;
        if (share_ids.empty()) {
            co_return result;
        }

        struct OrderedShareFile {
            uint64_t share_file_id;
            ShareFile share_file;
        };

        auto chunks = BatchUtils::Chunk(share_ids, DEFAULT_BATCH_CHUNK_SIZE);
        std::unordered_map<uint64_t, std::vector<OrderedShareFile>> ordered_items_by_share;

        try {
            for (const auto& chunk : chunks) {
                auto placeholders = BatchUtils::BuildInPlaceholders(chunk);

                auto file_rows = co_await m_db_client->execSqlCoro(
                    "SELECT sf.id AS share_file_id, sf.share_id, f.id, f.name, f.size, fc.hash_md5, 'file' AS " "type " "FROM share_files sf " "JOIN files f ON sf.item_id = f.id " "LEFT JOIN file_contents fc ON f.content_id = fc.id " "WHERE sf.share_id IN (" + placeholders + ") AND sf.item_type = 'file' " "ORDER BY sf.id",
                    chunk
                );

                auto folder_rows = co_await m_db_client->execSqlCoro(
                    "SELECT sf.id AS share_file_id, sf.share_id, fo.id, fo.name, 0 AS size, " "fo.item_count, 'folder' AS type " "FROM share_files sf " "JOIN folders fo ON sf.item_id = fo.id " "WHERE sf.share_id IN (" + placeholders + ") AND sf.item_type = 'folder' " "ORDER BY sf.id",
                    chunk
                );

                for (const auto& row : file_rows) {
                    ShareFile share_file;
                    share_file.id = row["id"].as<uint64_t>();
                    share_file.name = row["name"].as<std::string>();
                    share_file.type = row["type"].as<std::string>();
                    share_file.size = row["size"].as<uint64_t>();
                    share_file.file_hash = row["hash_md5"].isNull() ? "" : row["hash_md5"].as<std::string>();
                    share_file.supports_range = true;
                    auto share_id = row["share_id"].as<uint64_t>();
                    ordered_items_by_share[share_id].push_back(OrderedShareFile{ .share_file_id = row["share_file_id"].as<uint64_t>(), .share_file = std::move(share_file) });
                }

                for (const auto& row : folder_rows) {
                    ShareFile share_file;
                    share_file.id = row["id"].as<uint64_t>();
                    share_file.name = row["name"].as<std::string>();
                    share_file.type = row["type"].as<std::string>();
                    share_file.size = row["size"].as<uint64_t>();
                    share_file.item_count = row["item_count"].as<uint32_t>();
                    auto share_id = row["share_id"].as<uint64_t>();
                    ordered_items_by_share[share_id].push_back(OrderedShareFile{ .share_file_id = row["share_file_id"].as<uint64_t>(), .share_file = std::move(share_file) });
                }
            }

            result.reserve(ordered_items_by_share.size());
            for (auto& [share_id, ordered_items] : ordered_items_by_share) {
                std::sort(
                    ordered_items.begin(),
                    ordered_items.end(),
                    [](const OrderedShareFile& lhs, const OrderedShareFile& rhs) {
                        return lhs.share_file_id < rhs.share_file_id;
                    }
                );

                auto& share_files = result[share_id];
                share_files.reserve(ordered_items.size());
                for (auto& ordered_item : ordered_items) {
                    share_files.push_back(std::move(ordered_item.share_file));
                }
            }
        } catch (const DrogonDbException& e) {
            Logger::Error(log_context)
                << "Failed to get share file list: " << e.base().what();
        }

        co_return result;
    }

    auto ShareService::IsShareExpired(const Shares& share) -> bool {
        if (share.getExpiresAt() == nullptr) {
            return false;
        }
        return share.getValueOfExpiresAt() < trantor::Date::now();
    }

    auto ShareService::IsShareActive(const Shares& share) -> bool {
        if (share.getValueOfStatus() != static_cast<int8_t>(ShareStatus::Active)) {
            return false;
        }
        if (share.getExpiresAt() != nullptr && IsShareExpired(share)) {
            return false;
        }
        return true;
    }

    auto ShareService::ValidateShareActive(
        uint64_t share_id,
        disk::utils::LogContext log_context
    ) const -> drogon::Task<Result<void>> {
        try {
            CoroMapper<Shares> share_mapper(m_db_client);
            auto share = co_await share_mapper.findByPrimaryKey(share_id);
            if (!IsShareActive(share)) {
                co_return std::unexpected(ErrorInfo(ErrorCode::ShareExpired, "Share is not active"));
            }
        } catch (const UnexpectedRows&) {
            co_return std::unexpected(ErrorInfo(ErrorCode::ShareNotFound, "Share not found"));
        } catch (const DrogonDbException& e) {
            Logger::Error(log_context)
                << "Failed to validate share active state: " << e.base().what();
            co_return std::unexpected(
                ErrorInfo(ErrorCode::InternalError, "Failed to validate share status")
            );
        }

        co_return {};
    }

    auto ShareService::VerifyPassword(const Shares& share, const std::string& password) -> bool {
        if (share.getPasswordHash() == nullptr) {
            return true;
        }
        return utils::HashUtil::VerifyPassword(password, share.getValueOfPasswordHash());
    }

    auto ShareService::IncrementViewCount(
        uint64_t share_id,
        disk::utils::LogContext log_context
    ) -> drogon::Task<void> {
        try {
            co_await m_db_client->execSqlCoro(
                "UPDATE shares SET view_count = view_count + 1 WHERE id = $1",
                share_id
            );
        } catch (const DrogonDbException& e) {
            Logger::Error(log_context) << "Failed to update view count: " << e.base().what();
        }
    }

    auto ShareService::IncrementDownloadCount(
        uint64_t share_id,
        disk::utils::LogContext log_context
    ) -> drogon::Task<void> {
        try {
            co_await m_db_client->execSqlCoro(
                "UPDATE shares SET download_count = download_count + 1 WHERE id = $1",
                share_id
            );
        } catch (const DrogonDbException& e) {
            Logger::Error(log_context)
                << "Failed to update download count: " << e.base().what();
        }
    }

    auto ShareService::UpdateFileDownloadMetadata(
        uint64_t file_id,
        disk::utils::LogContext log_context
    ) -> drogon::Task<void> {
        try {
            co_await m_db_client->execSqlCoro(
                "UPDATE files " "SET download_count = download_count + 1, last_accessed_at = NOW() " "WHERE id = $1",
                file_id
            );
        } catch (const DrogonDbException& e) {
            Logger::Error(log_context)
                << "Failed to update shared file download metadata: " << e.base().what()
                << " (file_id=" << file_id << ")";
        }
    }

    auto ShareService::GetStatusFilter(const std::string& status) -> std::optional<int8_t> {
        if (status == "all") {
            return std::nullopt;
        }
        if (status == "active") {
            return static_cast<int8_t>(ShareStatus::Active);
        }
        if (status == "expired") {
            return static_cast<int8_t>(ShareStatus::Expired);
        }
        if (status == "cancelled") {
            return static_cast<int8_t>(ShareStatus::Cancelled);
        }
        return std::nullopt;
    }

    auto ShareService::FormatDateTime(const trantor::Date& date) -> std::string {
        auto micro_seconds = date.microSecondsSinceEpoch();
        auto seconds = micro_seconds / 1000000;
        auto tm = *std::localtime(&seconds);

        std::ostringstream oss;
        oss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
        return oss.str();
    }

    auto ShareService::BuildShareLink(const std::string& share_code) -> std::string {
        return "/s/" + share_code;
    }

    auto ShareService::HandleFailedShareAccess(
        const std::string& share_code,
        const std::string& ip_address,
        disk::utils::LogContext log_context
    ) const -> drogon::Task<FailedShareAccessResult> {
        const auto validation_failure = [](uint64_t attempt_count, bool counter_available) {
            return FailedShareAccessResult{
                .error = ErrorInfo(
                    ErrorCode::SharePasswordError,
                    std::string(SHARE_ACCESS_VALIDATION_ERROR_MESSAGE)
                ),
                .attempt_count = attempt_count,
                .counter_available = counter_available,
            };
        };

        if (!m_redis_service) {
            co_return validation_failure(0, false);
        }

        auto key = redis::RedisKeyPrefix::BuildSharePasswordRateLimitKey(share_code, ip_address);

        /// 单个 Lua 操作原子递增，并仅在首次失败时设置固定窗口。
        auto incr_result = co_await m_redis_service->IncrWithExpire(
            key,
            SHARE_ACCESS_FAILURE_WINDOW_SECONDS,
            log_context
        );
        if (!incr_result) {
            Logger::Error(log_context)
                << "Redis IncrWithExpire failed: " << incr_result.error().message;
            co_return validation_failure(0, false);
        }

        const auto attempt_count = static_cast<uint64_t>(incr_result.value());
        if (attempt_count > SHARE_ACCESS_FAILURE_LIMIT) {
            co_return FailedShareAccessResult{
                .error = ErrorInfo(
                    ErrorCode::TooManyRequests,
                    std::string(SHARE_ACCESS_RATE_LIMIT_ERROR_MESSAGE)
                ),
                .attempt_count = attempt_count,
                .counter_available = true,
            };
        }

        co_return validation_failure(attempt_count, true);
    }

    auto ShareService::RecordFailedShareAccess(
        std::optional<uint64_t> share_id,
        const std::string& share_code,
        const FailedShareAccessResult& failure,
        const ShareAuditContext& audit_context,
        disk::utils::LogContext log_context
    ) const -> drogon::Task<void> {
        const auto rate_limited = failure.error.code == ErrorCode::TooManyRequests;
        co_await m_audit_service.RecordPasswordFailure(SharePasswordFailureAuditEvent{
            .share_id = share_id,
            .share_code = share_code,
            .attempt_count = failure.attempt_count,
            .counter_available = failure.counter_available,
            .rate_limited = rate_limited,
            .context = audit_context,
            .log_context = log_context,
        });
        co_await m_audit_service.RecordAccess(ShareAccessAuditEvent{
            .share_id = share_id,
            .share_code = share_code,
            .success = false,
            .result = rate_limited ? "rate_limited" : "validation_failed",
            .context = audit_context,
            .log_context = log_context,
        });
    }
} // namespace disk::share
