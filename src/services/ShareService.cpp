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
#include <limits>
#include <random>
#include <unordered_map>
#include <unordered_set>

#include <drogon/orm/CoroMapper.h>
#include <trantor/utils/Date.h>

#include "models/Files.hpp"
#include "models/Folders.hpp"
#include "models/ShareFiles.hpp"
#include "models/Shares.hpp"
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

    // ==================== 构造函数 ====================

    ShareService::ShareService(
        DbClientPtr db_client,
        drogon::nosql::RedisClientPtr redis_client,
        std::string jwt_secret
    )
        : m_db_client(std::move(db_client)),
          m_redis_client(std::move(redis_client)),
          m_redis_service(disk::services::RedisService::GetInstance()),
          m_jwt_secret(std::move(jwt_secret)) {
        // 初始化 RedisService 单例（如果尚未初始化）
        disk::services::RedisService::Initialize(m_redis_client);
        LOG_DEBUG << "ShareService initialization completed";
    }

    // ==================== 公共方法 ====================

    auto ShareService::Create(CreateShareRequest request, uint64_t user_id)
        -> drogon::Task<Result<CreateShareResponse>> {
        LOG_INFO << "Creating share: user_id=" << user_id
                 << ", file_ids.size()=" << request.file_ids.size();

        // 1. 验证文件所有权
        auto files_result = co_await ValidateFileOwnership(request.file_ids, user_id);
        if (!files_result) {
            co_return std::unexpected(files_result.error());
        }
        const auto& files = *files_result;

        // 2. 生成分享码
        auto share_code = GenerateShareCode();

        // 3. 计算过期时间
        auto now = trantor::Date::now();
        std::optional<trantor::Date> expires_at;
        if (request.expire_days > 0) {
            expires_at = now.after(request.expire_days * 86400);
        }

        // 4. 哈希密码（如果有）
        std::optional<std::string> password_hash;
        if (request.password.has_value() && !request.password->empty()) {
            auto hash_result = utils::HashUtil::HashPassword(*request.password);
            if (!hash_result) {
                LOG_ERROR << "Password hashing failed";
                co_return std::unexpected(
                    ErrorInfo(ErrorCode::InternalError, "Password encryption failed")
                );
            }
            password_hash = *hash_result;
        }

        // 5. 创建分享记录 + 分享文件关联（事务保证原子性）
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
            transaction = co_await m_db_client->newTransactionCoro();

            // 插入分享行
            CoroMapper<Shares> share_mapper(transaction);
            created_share = co_await share_mapper.insert(share);

            // 批量插入 share_files 关联
            if (!files.empty()) {
                auto chunks = BatchUtils::Chunk(files, DEFAULT_BATCH_CHUNK_SIZE);
                for (const auto& chunk : chunks) {
                    std::string insert_sql =
                        "INSERT INTO share_files (share_id, item_type, item_id, created_at) VALUES ";
                    for (size_t i = 0; i < chunk.size(); ++i) {
                        if (i > 0) {
                            insert_sql += ", ";
                        }
                        insert_sql += "(?, ?, ?, ?)";
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
        } catch (const DrogonDbException& e) {
            LOG_ERROR << "Failed to create share (transaction): " << e.base().what();
            if (transaction) {
                try {
                    transaction->rollback();
                } catch (const std::exception& rollback_e) {
                    LOG_ERROR << "Transaction rollback failed: " << rollback_e.what();
                }
            }
            co_return std::unexpected(
                ErrorInfo(ErrorCode::InternalError, "Failed to create share")
            );
        }

        // 7. 构建响应
        CreateShareResponse response;
        response.share_id = share_code;
        response.share_link = BuildShareLink(share_code);
        if (request.password.has_value() && !request.password->empty()) {
            response.password = *request.password;
        }
        response.permission = SharePermissionToString(request.permission);
        response.expires_at = expires_at.has_value() ? FormatDateTime(*expires_at) : "";
        response.created_at = FormatDateTime(created_share.getValueOfCreatedAt());

        LOG_INFO << "Share created successfully: share_code=" << share_code;
        co_return response;
    }

    auto ShareService::List(const ShareListRequest& request, uint64_t user_id)
        -> drogon::Task<Result<ShareListResponse>> {
        LOG_DEBUG << "Getting share list: user_id=" << user_id << ", status=" << request.status;

        CoroMapper<Shares> mapper(m_db_client);

        // 构建查询条件
        auto criteria = Criteria(Shares::Cols::_user_id, user_id);

        auto status_filter = GetStatusFilter(request.status);
        if (status_filter.has_value()) {
            criteria = criteria && Criteria(Shares::Cols::_status, *status_filter);
        }

        // 获取总数
        int total = 0;
        try {
            total = co_await mapper.count(criteria);
        } catch (const DrogonDbException& e) {
            LOG_ERROR << "Failed to get share count: " << e.base().what();
            co_return std::unexpected(
                ErrorInfo(ErrorCode::InternalError, "Failed to get share list")
            );
        }

        // 分页查询
        std::vector<Shares> shares;
        try {
            shares = co_await mapper.orderBy(Shares::Cols::_created_at, SortOrder::DESC)
                         .offset((request.page - 1) * request.page_size)
                         .limit(request.page_size)
                         .findBy(criteria);
        } catch (const DrogonDbException& e) {
            LOG_ERROR << "Failed to get share list: " << e.base().what();
            co_return std::unexpected(
                ErrorInfo(ErrorCode::InternalError, "Failed to get share list")
            );
        }

        // 构建响应
        ShareListResponse response;
        std::vector<uint64_t> share_ids;
        share_ids.reserve(shares.size());
        for (const auto& share : shares) {
            share_ids.push_back(share.getValueOfId());
        }

        auto share_files_map = co_await GetShareFilesBatch(share_ids);

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

            // 状态
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

        // 分页信息
        response.pagination.page = request.page;
        response.pagination.page_size = request.page_size;
        response.pagination.total = total;
        response.pagination.total_pages =
            request.page_size > 0 ? (total + request.page_size - 1) / request.page_size : 0;

        co_return response;
    }

    auto ShareService::Detail(const ShareDetailRequest& request, uint64_t user_id)
        -> drogon::Task<Result<ShareDetailResponse>> {
        LOG_DEBUG << "Getting share details: share_id=" << request.share_id
                  << ", user_id=" << user_id;

        // 验证分享所有权
        auto share_result = co_await ValidateShareOwnership(request.share_id, user_id);
        if (!share_result) {
            co_return std::unexpected(share_result.error());
        }
        const auto& share = *share_result;

        // 验证分享状态（必须是有效状态）
        if (!IsShareActive(share)) {
            co_return std::unexpected(
                ErrorInfo(ErrorCode::ShareExpired, "Share cancelled or expired")
            );
        }

        // 获取分享文件列表
        auto share_files = co_await GetShareFiles(share.getValueOfId());

        // 构建响应
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

        // 状态
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

    auto ShareService::Update(const UpdateShareRequest& request, uint64_t user_id)
        -> drogon::Task<Result<UpdateShareResponse>> {
        LOG_INFO << "Updating share settings: share_id=" << request.share_id
                 << ", user_id=" << user_id;

        // 验证分享所有权
        auto share_result = co_await ValidateShareOwnership(request.share_id, user_id);
        if (!share_result) {
            co_return std::unexpected(share_result.error());
        }
        auto share = *share_result;

        // 验证分享状态（必须是有效状态）
        if (!IsShareActive(share)) {
            co_return std::unexpected(
                ErrorInfo(ErrorCode::ShareExpired, "Share cancelled or expired, cannot update")
            );
        }

        auto now = trantor::Date::now();

        // 更新过期时间
        if (request.expire_days.has_value()) {
            if (*request.expire_days > 0) {
                share.setExpiresAt(now.after(*request.expire_days * 86400));
            } else {
                share.setExpiresAtToNull();
            }
        }

        // 更新密码
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

        // 更新权限
        if (request.permission.has_value()) {
            share.setPermission(SharePermissionToString(*request.permission));
        }

        share.setUpdatedAt(now);

        // 保存更新
        CoroMapper<Shares> mapper(m_db_client);
        try {
            co_await mapper.update(share);
        } catch (const DrogonDbException& e) {
            LOG_ERROR << "Failed to update share: " << e.base().what();
            co_return std::unexpected(
                ErrorInfo(ErrorCode::InternalError, "Failed to update share")
            );
        }

        // 构建响应
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

        LOG_INFO << "Share updated successfully: share_id=" << request.share_id;
        co_return response;
    }

    auto ShareService::Cancel(const CancelShareRequest& request, uint64_t user_id)
        -> drogon::Task<Result<CancelShareResponse>> {
        LOG_INFO << "Batch cancel shares: user_id=" << user_id
                 << ", share_ids.size()=" << request.share_ids.size();

        CancelShareResponse response;
        response.summary.total = static_cast<int>(request.share_ids.size());
        response.summary.succeeded = 0;
        response.summary.failed = 0;

        if (!BatchUtils::ValidateBatchInput(
                request.share_ids,
                std::numeric_limits<size_t>::max()
            )) {
            LOG_INFO << "Batch cancel shares completed: succeeded=" << response.summary.succeeded
                     << ", failed=" << response.summary.failed;
            co_return response;
        }

        auto chunks = BatchUtils::Chunk(request.share_ids, DEFAULT_BATCH_CHUNK_SIZE);

        for (const auto& chunk : chunks) {
            auto placeholders = BatchUtils::BuildInPlaceholders(chunk);
            auto select_sql =
                "SELECT id, share_code, user_id, status, updated_at FROM shares WHERE share_code IN (" + placeholders + ")";

            struct ShareRow {
                uint64_t user_id;
                int8_t status;
            };

            std::unordered_map<std::string, ShareRow> share_map;
            try {
                auto select_rows = co_await m_db_client->execSqlCoro(select_sql, chunk);
                share_map.reserve(select_rows.size());
                for (const auto& row : select_rows) {
                    auto share_code = row["share_code"].as<std::string>();
                    share_map.emplace(
                        std::move(share_code),
                        ShareRow{ .user_id = row["user_id"].as<uint64_t>(),
                                  .status = row["status"].as<int8_t>() }
                    );
                }
            } catch (const DrogonDbException& e) {
                LOG_ERROR << "Failed to fetch shares for cancel: " << e.base().what();

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
                response.results.push_back(result);
            }

            if (!valid_codes_to_cancel.empty()) {
                std::vector<std::string> valid_share_codes(
                    valid_codes_to_cancel.begin(),
                    valid_codes_to_cancel.end()
                );

                auto update_placeholders = BatchUtils::BuildInPlaceholders(valid_share_codes);
                auto update_sql =
                    "UPDATE shares SET status = 0, updated_at = ? WHERE share_code IN (" + update_placeholders + ")";
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
                    LOG_ERROR << "Failed to cancel share: " << e.base().what();

                    for (auto& result : response.results) {
                        if (result.status == "success" && valid_codes_to_cancel.contains(result.share_id) && result.error == std::nullopt) {
                            result.status = "failed";
                            result.error = CancelShareError{ .code = static_cast<int>(ErrorCode::InternalError),
                                                             .message = "Operation failed",
                                                             .reason = "internal_error" };
                        }
                    }
                }
            }

            response.summary.succeeded = 0;
            response.summary.failed = 0;
            for (const auto& result : response.results) {
                if (result.status == "success") {
                    response.summary.succeeded++;
                } else {
                    response.summary.failed++;
                }
            }
        }

        LOG_INFO << "Batch cancel shares completed: succeeded=" << response.summary.succeeded
                 << ", failed=" << response.summary.failed;

        co_return response;
    }

    auto ShareService::Access(const AccessShareRequest& request, const std::string& ip_address)
        -> drogon::Task<Result<AccessShareResponse>> {
        LOG_INFO << "Verifying share access: share_id=" << request.share_id;

        // 查找分享
        auto share_result = co_await FindShareByCode(request.share_id);
        if (!share_result) {
            co_return std::unexpected(share_result.error());
        }
        const auto& share = *share_result;

        // 验证分享状态
        if (!IsShareActive(share)) {
            co_return std::unexpected(
                ErrorInfo(ErrorCode::ShareExpired, "Share has been cancelled")
            );
        }

        // 验证是否过期
        if (share.getExpiresAt() != nullptr && IsShareExpired(share)) {
            co_return std::unexpected(ErrorInfo(ErrorCode::ShareExpired, "Share has expired"));
        }

        // 验证密码
        if (share.getPasswordHash() != nullptr) {
            // 检查密码尝试限制 (原子递增)
            auto limit_result = co_await CheckPasswordRateLimit(request.share_id, ip_address);
            if (!limit_result) {
                co_return std::unexpected(limit_result.error());
            }

            if (!request.password.has_value() || request.password->empty()) {
                co_return std::unexpected(
                    ErrorInfo(ErrorCode::SharePasswordError, "Access password required")
                );
            }

            if (!VerifyPassword(share, *request.password)) {
                co_return std::unexpected(
                    ErrorInfo(ErrorCode::SharePasswordError, "Incorrect access password")
                );
            }
        }
        auto token_result = services::TokenService::GenerateShareToken(
            m_jwt_secret,
            share.getValueOfShareCode(),
            share.getValueOfId()
        );
        if (!token_result) {
            co_return std::unexpected(token_result.error());
        }

        // 增加访问次数
        co_await IncrementViewCount(share.getValueOfId());

        // 获取分享文件列表
        auto share_files = co_await GetShareFiles(share.getValueOfId());

        // 构建响应
        AccessShareResponse response;
        response.share_token = *token_result;
        response.expires_in = services::TokenService::GetShareTokenExpireSeconds();
        response.permission = share.getValueOfPermission();
        response.files = share_files;

        LOG_INFO << "Share access verified successfully: share_id=" << request.share_id;
        co_return response;
    }

    auto ShareService::Browse(const BrowseShareRequest& request, uint64_t share_id)
        -> drogon::Task<Result<BrowseShareResponse>> {
        LOG_DEBUG << "Browsing share content: share_id=" << request.share_id
                  << ", internal_share_id=" << share_id;

        // 获取分享的文件列表
        auto share_files = co_await GetShareFiles(share_id);

        BrowseShareResponse response;

        // 构建浏览项
        for (const auto& file : share_files) {
            BrowseItem item;
            item.id = file.id;
            item.name = file.name;
            item.type = file.type;
            item.size = file.size;
            response.items.push_back(item);
        }

        // 面包屑导航（简化版：仅显示根目录）
        BrowseBreadcrumb root;
        root.id = 0;
        root.name = "root";
        response.breadcrumb.push_back(root);

        co_return response;
    }

    auto ShareService::DownloadMeta(const DownloadShareRequest& request, uint64_t share_id)
        -> drogon::Task<Result<ShareFile>> {
        LOG_DEBUG << "Getting download metadata: share_id=" << request.share_id
                  << ", file_id=" << request.file_id;

        // 单次 JOIN 查询：验证文件属于分享内容并获取元数据
        try {
            auto rows = co_await m_db_client->execSqlCoro(
                "SELECT sf.id AS share_file_id, f.id, f.name, f.size, 'file' AS type " "FROM share_files sf " "JOIN files f ON sf.item_id = f.id " "WHERE sf.share_id = ? AND sf.item_type = 'file' AND sf.item_id = ?",
                share_id,
                request.file_id
            );

            if (rows.empty()) {
                co_return std::unexpected(ErrorInfo(ErrorCode::FileNotFound, "File not in share"));
            }

            const auto& row = rows[0];
            ShareFile file;
            file.id = row["id"].as<uint64_t>();
            file.name = row["name"].as<std::string>();
            file.type = row["type"].as<std::string>();
            file.size = row["size"].as<uint64_t>();

            // 增加下载次数
            co_await IncrementDownloadCount(share_id);
            co_return file;
        } catch (const DrogonDbException& e) {
            LOG_ERROR << "Failed to get download metadata: " << e.base().what();
            co_return std::unexpected(
                ErrorInfo(ErrorCode::InternalError, "Failed to get download metadata")
            );
        }
    }

    auto ShareService::GetDownloadInfo(const DownloadShareRequest& request, uint64_t share_id)
        -> drogon::Task<Result<DownloadInfo>> {
        LOG_DEBUG << "Getting download info: share_id=" << request.share_id
                  << ", file_id=" << request.file_id;

        // 单次 4 表 JOIN 查询：shares + share_files + files + file_contents
        try {
            auto rows = co_await m_db_client->execSqlCoro(
                "SELECT s.id AS share_id, s.permission, " "f.id AS file_id, f.name AS file_name, f.size AS file_size, f.content_id, " "fc.storage_path, fc.hash_md5, fc.mime_type " "FROM shares s " "JOIN share_files sf ON s.id = sf.share_id " "AND sf.item_type = 'file' AND sf.item_id = ? " "JOIN files f ON sf.item_id = f.id " "LEFT JOIN file_contents fc ON f.content_id = fc.id " "WHERE s.id = ?",
                request.file_id,
                share_id
            );

            if (rows.empty()) {
                co_return std::unexpected(
                    ErrorInfo(ErrorCode::FileNotFound, "File not in share")
                );
            }

            const auto& row = rows[0];
            auto permission = row["permission"].as<std::string>();

            if (permission != "download") {
                LOG_WARN << "Insufficient share permissions: share_id=" << share_id
                         << ", permission=" << permission;
                co_return std::unexpected(
                    ErrorInfo(ErrorCode::ShareAccessDenied, "Share is view-only, download not allowed")
                );
            }

            DownloadInfo info;
            info.file_id = row["file_id"].as<uint64_t>();
            info.filename = row["file_name"].as<std::string>();
            info.storage_path = row["storage_path"].as<std::string>();
            info.file_size = row["file_size"].as<uint64_t>();
            info.mime_type =
                row["mime_type"].isNull() ? "application/octet-stream" : row["mime_type"].as<std::string>();
            info.hash_md5 = row["hash_md5"].as<std::string>();

            co_return info;
        } catch (const DrogonDbException& e) {
            LOG_ERROR << "Failed to get download info: " << e.base().what();
            co_return std::unexpected(
                ErrorInfo(ErrorCode::InternalError, "Failed to get download info")
            );
        }
    }

    auto ShareService::FindShareByCode(const std::string& share_code) const
        -> drogon::Task<Result<Shares>> {
        CoroMapper<Shares> mapper(m_db_client);

        try {
            auto share = co_await mapper.findOne(Criteria(Shares::Cols::_share_code, share_code));
            co_return share;
        } catch (const DrogonDbException& e) {
            const auto error_msg = std::string(e.base().what());
            // findOne() 抛出异常时，可能是"未找到"或真正的 DB 错误
            if (error_msg.find("empty") != std::string::npos ||
                error_msg.find("condition") != std::string::npos) {
                co_return std::unexpected(ErrorInfo(ErrorCode::ShareNotFound, "Share not found"));
            }
            LOG_ERROR << "Failed to find share: " << e.base().what();
            co_return std::unexpected(ErrorInfo(ErrorCode::InternalError, "Failed to find share"));
        }
    }

    // ==================== 私有方法 ====================

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
        uint64_t user_id
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
                "SELECT f.* FROM files f WHERE f.user_id = ? AND f.id IN (" + in_clause + ")";
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
                    LOG_WARN << "File not found or not owned by user: file_id=" << file_id;
                    co_return std::unexpected(
                        ErrorInfo(ErrorCode::FileNotFound, "File not found or no permission")
                    );
                }
                files.push_back(matched_files[it->second]);
            }
        } catch (const DrogonDbException& e) {
            LOG_ERROR << "Failed to validate file ownership: " << e.base().what();
            co_return std::unexpected(
                ErrorInfo(ErrorCode::InternalError, "Failed to validate file ownership")
            );
        }

        co_return files;
    }

    auto ShareService::ValidateShareOwnership(const std::string& share_code, uint64_t user_id) const
        -> drogon::Task<Result<Shares>> {
        auto share_result = co_await FindShareByCode(share_code);
        if (!share_result) {
            co_return std::unexpected(share_result.error());
        }

        if (share_result->getValueOfUserId() != user_id) {
            co_return std::unexpected(ErrorInfo(ErrorCode::ShareAccessDenied, "Access denied"));
        }

        co_return *share_result;
    }

    auto ShareService::GetShareFiles(uint64_t share_id) const
        -> drogon::Task<std::vector<ShareFile>> {
        std::vector<ShareFile> result;

        struct OrderedShareFile {
            uint64_t share_file_id;
            ShareFile share_file;
        };

        try {
            auto file_rows = co_await m_db_client->execSqlCoro(
                "SELECT sf.id AS share_file_id, f.id, f.name, f.size, 'file' AS type " "FROM share_files sf " "JOIN files f ON sf.item_id = f.id " "WHERE sf.share_id = ? AND sf.item_type = 'file' " "ORDER BY sf.id",
                share_id
            );

            auto folder_rows = co_await m_db_client->execSqlCoro(
                "SELECT sf.id AS share_file_id, fo.id, fo.name, 0 AS size, 'folder' AS type " "FROM share_files sf " "JOIN folders fo ON sf.item_id = fo.id " "WHERE sf.share_id = ? AND sf.item_type = 'folder' " "ORDER BY sf.id",
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
            LOG_ERROR << "Failed to get share file list: " << e.base().what();
        }

        co_return result;
    }

    auto ShareService::GetShareFilesBatch(const std::vector<uint64_t>& share_ids) const
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
                    "SELECT sf.id AS share_file_id, sf.share_id, f.id, f.name, f.size, 'file' AS " "type " "FROM share_files sf " "JOIN files f ON sf.item_id = f.id " "WHERE sf.share_id IN (" + placeholders + ") AND sf.item_type = 'file' " "ORDER BY sf.id",
                    chunk
                );

                auto folder_rows = co_await m_db_client->execSqlCoro(
                    "SELECT sf.id AS share_file_id, sf.share_id, fo.id, fo.name, 0 AS size, " "'folder' AS type " "FROM share_files sf " "JOIN folders fo ON sf.item_id = fo.id " "WHERE sf.share_id IN (" + placeholders + ") AND sf.item_type = 'folder' " "ORDER BY sf.id",
                    chunk
                );

                for (const auto& row : file_rows) {
                    ShareFile share_file;
                    share_file.id = row["id"].as<uint64_t>();
                    share_file.name = row["name"].as<std::string>();
                    share_file.type = row["type"].as<std::string>();
                    share_file.size = row["size"].as<uint64_t>();
                    auto share_id = row["share_id"].as<uint64_t>();
                    ordered_items_by_share[share_id].push_back(OrderedShareFile{ .share_file_id = row["share_file_id"].as<uint64_t>(), .share_file = std::move(share_file) });
                }

                for (const auto& row : folder_rows) {
                    ShareFile share_file;
                    share_file.id = row["id"].as<uint64_t>();
                    share_file.name = row["name"].as<std::string>();
                    share_file.type = row["type"].as<std::string>();
                    share_file.size = row["size"].as<uint64_t>();
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
            LOG_ERROR << "Failed to get share file list: " << e.base().what();
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

    auto ShareService::VerifyPassword(const Shares& share, const std::string& password) -> bool {
        if (share.getPasswordHash() == nullptr) {
            return true;
        }
        return utils::HashUtil::VerifyPassword(password, share.getValueOfPasswordHash());
    }

    auto ShareService::IncrementViewCount(uint64_t share_id) -> drogon::Task<void> {
        try {
            co_await m_db_client->execSqlCoro(
                "UPDATE shares SET view_count = view_count + 1 WHERE id = ?",
                share_id
            );
        } catch (const DrogonDbException& e) {
            LOG_ERROR << "Failed to update view count: " << e.base().what();
        }
    }

    auto ShareService::IncrementDownloadCount(uint64_t share_id) -> drogon::Task<void> {
        try {
            co_await m_db_client->execSqlCoro(
                "UPDATE shares SET download_count = download_count + 1 WHERE id = ?",
                share_id
            );
        } catch (const DrogonDbException& e) {
            LOG_ERROR << "Failed to update download count: " << e.base().what();
        }
    }

    auto ShareService::UpdateTimestamp(uint64_t share_id) -> drogon::Task<void> {
        try {
            co_await m_db_client->execSqlCoro(
                "UPDATE shares SET updated_at = ? WHERE id = ?",
                trantor::Date::now(),
                share_id
            );
        } catch (const DrogonDbException& e) {
            LOG_ERROR << "Failed to update timestamp: " << e.base().what();
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

    auto ShareService::CheckPasswordRateLimit(
        const std::string& share_code,
        const std::string& ip_address
    ) const -> drogon::Task<Result<void>> {
        if (!m_redis_service) {
            co_return {};
        }

        auto key = redis::RedisKeyPrefix::BuildSharePasswordRateLimitKey(share_code, ip_address);

        // 使用 Lua 脚本原子递增计数并设置过期时间（单次 Redis 交互）
        auto incr_result = co_await m_redis_service->IncrWithExpire(key, 900);

        if (!incr_result) {
            LOG_ERROR << "Redis IncrWithExpire failed: " << incr_result.error().message;
            co_return {};
        }

        constexpr int MAX_ATTEMPTS = 5;
        const int64_t count = incr_result.value();

        // 检查是否超过限制
        if (count > MAX_ATTEMPTS) {
            co_return std::unexpected(ErrorInfo(
                ErrorCode::TooManyRequests,
                "Too many password verification attempts, please try again later"
            ));
        }

        co_return {};
    }
} // namespace disk::share
