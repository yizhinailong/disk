/**
 * @file ShareService.cpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 分享服务实现
 * @version 0.1
 * @date 2026-02-15
 *
 * @copyright Copyright (c) 2026
 *
 */

#include "services/ShareService.hpp"

#include <algorithm>
#include <chrono>
#include <random>
#include <sstream>

#include <drogon/orm/CoroMapper.h>
#include <trantor/utils/Date.h>

#include "models/FileContents.hpp"
#include "models/Files.hpp"
#include "models/Folders.hpp"
#include "models/ShareFiles.hpp"
#include "models/Shares.hpp"
#include "utils/HashUtil.hpp"
#include "utils/RedisKeyPrefix.hpp"

namespace disk::share {

    using namespace drogon::orm;
    using disk::error::Result;
    using drogon_model::disk::Files;
    using drogon_model::disk::Folders;
    using drogon_model::disk::ShareFiles;
    using drogon_model::disk::Shares;

    // ==================== 构造函数 ====================

    ShareService::ShareService(
        DbClientPtr db_client,
        drogon::nosql::RedisClientPtr redis_client,
        std::string jwt_secret
    )
        : m_db_client(std::move(db_client)),
          m_redis_client(std::move(redis_client)),
          m_redis_service(std::make_shared<disk::services::RedisService>(m_redis_client)),
          m_jwt_secret(std::move(jwt_secret)) {}

    // ==================== 公共方法 ====================

    auto ShareService::Create(CreateShareRequest request, uint64_t user_id)
        -> drogon::Task<Result<CreateShareResponse>> {
        LOG_INFO << "创建分享: user_id=" << user_id << ", file_ids.size()=" << request.file_ids.size();

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
                LOG_ERROR << "密码哈希失败";
                co_return std::unexpected(ErrorInfo(ErrorCode::InternalError, "密码加密失败"));
            }
            password_hash = *hash_result;
        }

        // 5. 创建分享记录
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

        CoroMapper<Shares> share_mapper(m_db_client);
        Shares created_share;
        try {
            created_share = co_await share_mapper.insert(share);
        } catch (const DrogonDbException& e) {
            LOG_ERROR << "创建分享失败: " << e.base().what();
            co_return std::unexpected(ErrorInfo(ErrorCode::InternalError, "创建分享失败"));
        }

        // 6. 创建分享文件关联
        CoroMapper<ShareFiles> sf_mapper(m_db_client);
        for (const auto& file : files) {
            ShareFiles sf;
            sf.setShareId(created_share.getValueOfId());
            sf.setItemType("file");
            sf.setItemId(file.getValueOfId());
            sf.setCreatedAt(now);

            try {
                co_await sf_mapper.insert(sf);
            } catch (const DrogonDbException& e) {
                LOG_ERROR << "创建分享文件关联失败: " << e.base().what();
                co_return std::unexpected(ErrorInfo(ErrorCode::InternalError, "创建分享文件关联失败"));
            }
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

        LOG_INFO << "分享创建成功: share_code=" << share_code;
        co_return response;
    }

    auto ShareService::List(const ShareListRequest& request, uint64_t user_id)
        -> drogon::Task<Result<ShareListResponse>> {
        LOG_DEBUG << "获取分享列表: user_id=" << user_id << ", status=" << request.status;

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
            LOG_ERROR << "获取分享数量失败: " << e.base().what();
            co_return std::unexpected(ErrorInfo(ErrorCode::InternalError, "获取分享列表失败"));
        }

        // 分页查询
        std::vector<Shares> shares;
        try {
            shares = co_await mapper
                         .orderBy(Shares::Cols::_created_at, SortOrder::DESC)
                         .offset((request.page - 1) * request.page_size)
                         .limit(request.page_size)
                         .findBy(criteria);
        } catch (const DrogonDbException& e) {
            LOG_ERROR << "获取分享列表失败: " << e.base().what();
            co_return std::unexpected(ErrorInfo(ErrorCode::InternalError, "获取分享列表失败"));
        }

        // 构建响应
        ShareListResponse response;
        for (const auto& share : shares) {
            // 获取分享的文件列表
            auto share_files = co_await GetShareFiles(share.getValueOfId());

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
        response.pagination.total_pages = request.page_size > 0 ? (total + request.page_size - 1) / request.page_size : 0;

        co_return response;
    }

    auto ShareService::Detail(const ShareDetailRequest& request, uint64_t user_id)
        -> drogon::Task<Result<ShareDetailResponse>> {
        LOG_DEBUG << "获取分享详情: share_id=" << request.share_id << ", user_id=" << user_id;

        // 验证分享所有权
        auto share_result = co_await ValidateShareOwnership(request.share_id, user_id);
        if (!share_result) {
            co_return std::unexpected(share_result.error());
        }
        const auto& share = *share_result;

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
        LOG_INFO << "更新分享设置: share_id=" << request.share_id << ", user_id=" << user_id;

        // 验证分享所有权
        auto share_result = co_await ValidateShareOwnership(request.share_id, user_id);
        if (!share_result) {
            co_return std::unexpected(share_result.error());
        }
        auto share = *share_result;

        // 验证分享状态（必须是有效状态）
        if (!IsShareActive(share)) {
            co_return std::unexpected(ErrorInfo(ErrorCode::ShareExpired, "分享已取消或已过期，无法更新"));
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
                    co_return std::unexpected(ErrorInfo(ErrorCode::InternalError, "密码加密失败"));
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
            LOG_ERROR << "更新分享失败: " << e.base().what();
            co_return std::unexpected(ErrorInfo(ErrorCode::InternalError, "更新分享失败"));
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

        LOG_INFO << "分享更新成功: share_id=" << request.share_id;
        co_return response;
    }

    auto ShareService::Cancel(const CancelShareRequest& request, uint64_t user_id)
        -> drogon::Task<Result<CancelShareResponse>> {
        LOG_INFO << "批量取消分享: user_id=" << user_id << ", share_ids.size()=" << request.share_ids.size();

        CancelShareResponse response;
        response.summary.total = static_cast<int>(request.share_ids.size());
        response.summary.succeeded = 0;
        response.summary.failed = 0;

        CoroMapper<Shares> mapper(m_db_client);

        for (const auto& share_id : request.share_ids) {
            CancelShareResult result;
            result.share_id = share_id;

            // 查找分享
            auto shares = co_await mapper.findBy(Criteria(Shares::Cols::_share_code, share_id));

            if (shares.empty()) {
                result.status = "failed";
                result.error = CancelShareError{
                    .code = static_cast<int>(ErrorCode::ShareNotFound),
                    .message = "分享不存在",
                    .reason = "share_not_found"
                };
                response.results.push_back(result);
                response.summary.failed++;
                continue;
            }

            auto& share = shares[0];

            // 验证所有权
            if (share.getValueOfUserId() != user_id) {
                result.status = "failed";
                result.error = CancelShareError{
                    .code = static_cast<int>(ErrorCode::ShareAccessDenied),
                    .message = "无权限访问",
                    .reason = "access_denied"
                };
                response.results.push_back(result);
                response.summary.failed++;
                continue;
            }

            // 检查是否已取消
            if (share.getValueOfStatus() == static_cast<int8_t>(ShareStatus::Cancelled)) {
                result.status = "failed";
                result.error = CancelShareError{
                    .code = static_cast<int>(ErrorCode::ValidationFailed),
                    .message = "分享已取消",
                    .reason = "already_cancelled"
                };
                response.results.push_back(result);
                response.summary.failed++;
                continue;
            }

            // 取消分享
            share.setStatus(static_cast<int8_t>(ShareStatus::Cancelled));
            share.setUpdatedAt(trantor::Date::now());

            try {
                co_await mapper.update(share);
                result.status = "success";
                response.summary.succeeded++;
            } catch (const DrogonDbException& e) {
                LOG_ERROR << "取消分享失败: " << e.base().what();
                result.status = "failed";
                result.error = CancelShareError{
                    .code = static_cast<int>(ErrorCode::InternalError),
                    .message = "操作失败",
                    .reason = "internal_error"
                };
                response.summary.failed++;
            }

            response.results.push_back(result);
        }

        LOG_INFO << "批量取消分享完成: succeeded=" << response.summary.succeeded
                 << ", failed=" << response.summary.failed;

        co_return response;
    }

    auto ShareService::Access(const AccessShareRequest& request, const std::string& ip_address)
        -> drogon::Task<Result<AccessShareResponse>> {
        LOG_INFO << "验证分享访问: share_id=" << request.share_id;

        // 查找分享
        auto share_result = co_await FindShareByCode(request.share_id);
        if (!share_result) {
            co_return std::unexpected(share_result.error());
        }
        const auto& share = *share_result;

        // 验证分享状态
        if (!IsShareActive(share)) {
            co_return std::unexpected(ErrorInfo(ErrorCode::ShareExpired, "分享已取消"));
        }

        // 验证是否过期
        if (share.getExpiresAt() != nullptr && IsShareExpired(share)) {
            co_return std::unexpected(ErrorInfo(ErrorCode::ShareExpired, "分享已过期"));
        }

        // 验证密码
        if (share.getPasswordHash() != nullptr) {
            // 检查密码尝试限制
            auto limit_result = co_await CheckPasswordRateLimit(request.share_id, ip_address);
            if (!limit_result) {
                co_return std::unexpected(limit_result.error());
            }

            if (!request.password.has_value() || request.password->empty()) {
                co_return std::unexpected(ErrorInfo(ErrorCode::SharePasswordError, "需要输入访问密码"));
            }

            if (!VerifyPassword(share, *request.password)) {
                co_await RecordPasswordFailure(request.share_id, ip_address);
                co_return std::unexpected(ErrorInfo(ErrorCode::SharePasswordError, "访问密码错误"));
            }
        }

        // 生成分享令牌
        auto token_result = services::TokenService::GenerateShareToken(
            m_jwt_secret,
            share.getValueOfShareCode(),
            share.getValueOfId()
        );
        if (!token_result) {
            co_return std::unexpected(token_result.error());
        }

        // 存储分享令牌到 Redis
        if (m_redis_service) {
            auto token_hash_result = services::TokenService::ExtractShareTokenHash(*token_result);
            if (token_hash_result) {
                auto key = redis::RedisKeyPrefix::BuildShareTokenKey(
                    share.getValueOfShareCode(),
                    *token_hash_result
                );
                co_await m_redis_service->Set(key, "1", 3600);
            }
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

        LOG_INFO << "分享访问验证成功: share_id=" << request.share_id;
        co_return response;
    }

    auto ShareService::Browse(const BrowseShareRequest& request, uint64_t share_id)
        -> drogon::Task<Result<BrowseShareResponse>> {
        LOG_DEBUG << "浏览分享内容: share_id=" << request.share_id << ", internal_share_id=" << share_id;

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
        LOG_DEBUG << "获取下载元数据: share_id=" << request.share_id << ", file_id=" << request.file_id;

        // 获取分享文件列表
        auto share_files = co_await GetShareFiles(share_id);

        // 查找请求的文件
        for (const auto& file : share_files) {
            if (file.id == request.file_id) {
                // 增加下载次数
                co_await IncrementDownloadCount(share_id);
                co_return file;
            }
        }

        co_return std::unexpected(ErrorInfo(ErrorCode::FileNotFound, "文件不在分享中"));
    }

    auto ShareService::GetDownloadInfo(const DownloadShareRequest& request, uint64_t share_id)
        -> drogon::Task<Result<DownloadInfo>> {
        LOG_DEBUG << "获取下载信息: share_id=" << request.share_id << ", file_id=" << request.file_id;

        // 1. 获取分享信息，验证下载权限
        CoroMapper<Shares> share_mapper(m_db_client);
        Shares share;
        try {
            share = co_await share_mapper.findOne(Criteria(Shares::Cols::_id, share_id));
        } catch (const DrogonDbException& e) {
            LOG_ERROR << "获取分享信息失败: " << e.base().what();
            co_return std::unexpected(ErrorInfo(ErrorCode::ShareNotFound, "分享不存在"));
        }

        if (share.getValueOfPermission() != "download") {
            LOG_WARN << "分享权限不足: share_id=" << share_id
                     << ", permission=" << share.getValueOfPermission();
            co_return std::unexpected(ErrorInfo(ErrorCode::ShareAccessDenied, "分享设置为仅查看，不允许下载"));
        }

        // 2. 验证文件属于分享内容，获取文件基本信息
        CoroMapper<ShareFiles> sf_mapper(m_db_client);
        CoroMapper<Files> file_mapper(m_db_client);

        std::vector<ShareFiles> share_files;
        try {
            share_files = co_await sf_mapper.findBy(Criteria(ShareFiles::Cols::_share_id, share_id));
        } catch (const DrogonDbException& e) {
            LOG_ERROR << "获取分享文件关联失败: " << e.base().what();
            co_return std::unexpected(ErrorInfo(ErrorCode::InternalError, "获取分享文件失败"));
        }

        drogon_model::disk::Files file;
        bool found = false;

        for (const auto& sf : share_files) {
            if (sf.getValueOfItemType() == "file" && sf.getValueOfItemId() == request.file_id) {
                try {
                    file = co_await file_mapper.findOne(
                        Criteria(Files::Cols::_id, request.file_id)
                    );
                    found = true;
                    break;
                } catch (const DrogonDbException& e) {
                    LOG_WARN << "获取文件失败: file_id=" << request.file_id;
                    co_return std::unexpected(ErrorInfo(ErrorCode::FileNotFound, "文件不存在"));
                }
            }
        }

        if (!found) {
            co_return std::unexpected(ErrorInfo(ErrorCode::FileNotFound, "文件不在分享中"));
        }

        // 3. 获取文件内容（存储路径）
        CoroMapper<drogon_model::disk::FileContents> content_mapper(m_db_client);
        drogon_model::disk::FileContents content;

        try {
            content = co_await content_mapper.findOne(
                Criteria(drogon_model::disk::FileContents::Cols::_id, file.getValueOfContentId())
            );
        } catch (const DrogonDbException& e) {
            LOG_ERROR << "获取文件内容失败: " << e.base().what();
            co_return std::unexpected(ErrorInfo(ErrorCode::FileNotFound, "文件内容不存在"));
        }

        // 4. 构建下载信息
        DownloadInfo info;
        info.file_id = file.getValueOfId();
        info.filename = file.getValueOfName();
        info.storage_path = content.getValueOfStoragePath();
        info.file_size = file.getValueOfSize();
        info.mime_type = content.getMimeType() ? content.getValueOfMimeType() : "application/octet-stream";
        info.hash_md5 = content.getValueOfHashMd5();

        co_return info;
    }

    auto ShareService::FindShareByCode(const std::string& share_code) const
        -> drogon::Task<Result<Shares>> {
        CoroMapper<Shares> mapper(m_db_client);

        try {
            auto shares = co_await mapper.findBy(Criteria(Shares::Cols::_share_code, share_code));

            if (shares.empty()) {
                co_return std::unexpected(ErrorInfo(ErrorCode::ShareNotFound, "分享不存在"));
            }

            co_return shares[0];
        } catch (const DrogonDbException& e) {
            LOG_ERROR << "查找分享失败: " << e.base().what();
            co_return std::unexpected(ErrorInfo(ErrorCode::InternalError, "查找分享失败"));
        }
    }

    // ==================== 私有方法 ====================

    auto ShareService::GenerateShareCode() -> std::string {
        constexpr const char* chars = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
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

    auto ShareService::ValidateFileOwnership(const std::vector<uint64_t>& file_ids, uint64_t user_id) const
        -> drogon::Task<Result<std::vector<Files>>> {
        if (file_ids.empty()) {
            co_return std::unexpected(ErrorInfo(ErrorCode::InvalidParameter, "文件ID列表不能为空"));
        }

        CoroMapper<Files> mapper(m_db_client);
        std::vector<Files> files;
        files.reserve(file_ids.size());

        for (auto file_id : file_ids) {
            try {
                auto file_opt = co_await mapper.findOne(
                    Criteria(Files::Cols::_id, file_id) &&
                    Criteria(Files::Cols::_user_id, user_id)
                );

                files.push_back(file_opt);
            } catch (const DrogonDbException& e) {
                LOG_WARN << "文件不存在或不属于用户: file_id=" << file_id;
                co_return std::unexpected(ErrorInfo(ErrorCode::FileNotFound, "文件不存在或无权限"));
            }
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
            co_return std::unexpected(ErrorInfo(ErrorCode::ShareAccessDenied, "无权限访问"));
        }

        co_return *share_result;
    }

    auto ShareService::GetShareFiles(uint64_t share_id) const -> drogon::Task<std::vector<ShareFile>> {
        CoroMapper<ShareFiles> sf_mapper(m_db_client);
        CoroMapper<Files> file_mapper(m_db_client);
        CoroMapper<Folders> folder_mapper(m_db_client);

        std::vector<ShareFile> result;

        try {
            auto share_files = co_await sf_mapper.findBy(Criteria(ShareFiles::Cols::_share_id, share_id));

            for (const auto& sf : share_files) {
                if (sf.getValueOfItemType() == "file") {
                    try {
                        auto file = co_await file_mapper.findOne(
                            Criteria(Files::Cols::_id, sf.getValueOfItemId())
                        );

                        ShareFile sf_item;
                        sf_item.id = file.getValueOfId();
                        sf_item.name = file.getValueOfName();
                        sf_item.type = "file";
                        sf_item.size = file.getValueOfSize();
                        result.push_back(sf_item);
                    } catch (const DrogonDbException& e) {
                        LOG_WARN << "获取分享文件失败: file_id=" << sf.getValueOfItemId();
                    }
                } else if (sf.getValueOfItemType() == "folder") {
                    try {
                        auto folder = co_await folder_mapper.findOne(
                            Criteria(Folders::Cols::_id, sf.getValueOfItemId())
                        );

                        ShareFile sf_item;
                        sf_item.id = folder.getValueOfId();
                        sf_item.name = folder.getValueOfName();
                        sf_item.type = "folder";
                        sf_item.size = 0;
                        result.push_back(sf_item);
                    } catch (const DrogonDbException& e) {
                        LOG_WARN << "获取分享文件夹失败: folder_id=" << sf.getValueOfItemId();
                    }
                }
            }
        } catch (const DrogonDbException& e) {
            LOG_ERROR << "获取分享文件列表失败: " << e.base().what();
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
            LOG_ERROR << "更新访问次数失败: " << e.base().what();
        }
    }

    auto ShareService::IncrementDownloadCount(uint64_t share_id) -> drogon::Task<void> {
        try {
            co_await m_db_client->execSqlCoro(
                "UPDATE shares SET download_count = download_count + 1 WHERE id = ?",
                share_id
            );
        } catch (const DrogonDbException& e) {
            LOG_ERROR << "更新下载次数失败: " << e.base().what();
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
            LOG_ERROR << "更新时间戳失败: " << e.base().what();
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

    auto ShareService::CheckPasswordRateLimit(const std::string& share_code, const std::string& ip_address) const
        -> drogon::Task<Result<void>> {
        if (!m_redis_service) {
            co_return {};
        }

        auto key = redis::RedisKeyPrefix::BuildSharePasswordRateLimitKey(share_code, ip_address);
        auto count_result = co_await m_redis_service->Get(key);

        if (count_result.has_value()) {
            try {
                int count = std::stoi(*count_result);
                if (count >= 5) {
                    co_return std::unexpected(ErrorInfo(
                        ErrorCode::TooManyRequests,
                        "密码验证次数过多，请稍后再试"
                    ));
                }
            } catch (...) {
                // 忽略解析错误
            }
        }

        co_return {};
    }

    auto ShareService::RecordPasswordFailure(const std::string& share_code, const std::string& ip_address)
        -> drogon::Task<void> {
        if (!m_redis_service) {
            co_return;
        }

        auto key = redis::RedisKeyPrefix::BuildSharePasswordRateLimitKey(share_code, ip_address);
        auto count_result = co_await m_redis_service->Incr(key);

        // 第一次失败时设置过期时间（15分钟）
        if (count_result.has_value() && *count_result == 1) {
            co_await m_redis_service->Expire(key, 900);
        }
    }

} // namespace disk::share
