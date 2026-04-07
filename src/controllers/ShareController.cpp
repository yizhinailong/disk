/**
 * @file ShareController.cpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 分享控制器
 *
 * @copyright Copyright (c) 2026
 *
 */

#include "ShareController.hpp"

#include <filesystem>
#include <memory>

#include "DownloadResponder.hpp"
#include "dtos/ShareDto.hpp"
#include "storage/StorageMgr.hpp"
#include "utils/ConfigMgr.hpp"
#include "utils/Response.hpp"

namespace disk::share {

    ShareController::ShareController()
        : m_share_service(
              std::make_unique<ShareService>(
                  drogon::app().getDbClient(),
                  drogon::app().getRedisClient(),
                  disk::utils::ConfigMgr::GetInstance()->GetJwtSecret()
              )
          ),
          m_storage(storage::StorageMgr::GetStorage()) {
    }

    // ==================== 所有者端点（JWT 保护） ====================

    auto ShareController::Create(drogon::HttpRequestPtr request)
        -> drogon::Task<drogon::HttpResponsePtr> {

        LOG_INFO << "Received create share request: " << request->getPeerAddr().toIpPort();

        // 1. 解析并验证请求参数
        auto parse_result = CreateShareRequest::FromRequest(request);
        if (!parse_result) {
            LOG_WARN << "Create share request parameter validation failed: "
                     << parse_result.error().message;
            co_return Response::Error(parse_result.error());
        }
        LOG_DEBUG << "Create share parameter validation passed: file_ids.size()="
                  << parse_result->file_ids.size() << ", expire_days=" << parse_result->expire_days
                  << ", has_password=" << parse_result->password.has_value()
                  << ", permission=" << SharePermissionToString(parse_result->permission);

        // 2. 从请求属性获取 user_id（由 JwtAuthFilter 设置）
        const auto user_id = request->attributes()->get<uint64_t>("user_id");

        // 3. 调用 Service 层创建分享
        auto result = co_await m_share_service->Create(*parse_result, user_id);
        if (!result) {
            LOG_ERROR << "Create share failed: " << result.error().message
                      << " (user_id=" << user_id << ")";
            co_return Response::Error(result.error());
        }

        // 4. 构造响应
        LOG_INFO << "Create share successful: share_id=" << result->share_id
                 << " (user_id=" << user_id << ")";
        co_return Response::Success(result->ToJson());
    }

    auto ShareController::List(drogon::HttpRequestPtr request)
        -> drogon::Task<drogon::HttpResponsePtr> {

        LOG_INFO << "Received get share list request: " << request->getPeerAddr().toIpPort();

        // 1. 解析并验证请求参数
        auto parse_result = ShareListRequest::FromRequest(request);
        if (!parse_result) {
            LOG_WARN << "Share list request parameter validation failed: "
                     << parse_result.error().message;
            co_return Response::Error(parse_result.error());
        }
        LOG_DEBUG << "Share list parameter validation passed: status=" << parse_result->status
                  << ", page=" << parse_result->page << ", page_size=" << parse_result->page_size;

        // 2. 从请求属性获取 user_id（由 JwtAuthFilter 设置）
        const auto user_id = request->attributes()->get<uint64_t>("user_id");

        // 3. 调用 Service 层获取分享列表
        auto result = co_await m_share_service->List(*parse_result, user_id);
        if (!result) {
            LOG_ERROR << "Get share list failed: " << result.error().message
                      << " (user_id=" << user_id << ")";
            co_return Response::Error(result.error());
        }

        // 4. 构造响应
        LOG_INFO << "Get share list successful: items=" << result->items.size()
                 << ", total=" << result->pagination.total << " (user_id=" << user_id << ")";
        co_return Response::Success(result->ToJson());
    }

    auto ShareController::Detail(drogon::HttpRequestPtr request, std::string share_id)
        -> drogon::Task<drogon::HttpResponsePtr> {

        LOG_INFO << "Received get share details request: " << request->getPeerAddr().toIpPort()
                 << ", share_id=" << share_id;

        // 1. 解析并验证路径参数
        auto parse_result = ShareDetailRequest::FromPath(share_id);
        if (!parse_result) {
            LOG_WARN << "Share detail request parameter validation failed: "
                     << parse_result.error().message;
            co_return Response::Error(parse_result.error());
        }

        // 2. 从请求属性获取 user_id（由 JwtAuthFilter 设置）
        const auto user_id = request->attributes()->get<uint64_t>("user_id");

        // 3. 调用 Service 层获取分享详情
        auto result = co_await m_share_service->Detail(*parse_result, user_id);
        if (!result) {
            LOG_ERROR << "Get share details failed: " << result.error().message
                      << " (user_id=" << user_id << ", share_id=" << share_id << ")";
            co_return Response::Error(result.error());
        }

        // 4. 构造响应
        LOG_INFO << "Get share details successful: share_id=" << share_id
                 << ", files=" << result->files.size() << " (user_id=" << user_id << ")";
        co_return Response::Success(result->ToJson());
    }

    auto ShareController::Update(drogon::HttpRequestPtr request, std::string share_id)
        -> drogon::Task<drogon::HttpResponsePtr> {

        LOG_INFO << "Received update share settings request: " << request->getPeerAddr().toIpPort()
                 << ", share_id=" << share_id;

        // 1. 解析并验证请求参数
        auto parse_result = UpdateShareRequest::FromRequest(request, share_id);
        if (!parse_result) {
            LOG_WARN << "Update share settings request parameter validation failed: "
                     << parse_result.error().message;
            co_return Response::Error(parse_result.error());
        }
        LOG_DEBUG << "Update share settings parameter validation passed: share_id="
                  << parse_result->share_id << ", expire_days="
                  << (parse_result->expire_days.has_value() ?
                          std::to_string(*parse_result->expire_days) :
                          "null")
                  << ", password=" << (parse_result->password.has_value() ? "set" : "null")
                  << ", permission="
                  << (parse_result->permission.has_value() ?
                          SharePermissionToString(*parse_result->permission) :
                          "null");

        // 2. 从请求属性获取 user_id（由 JwtAuthFilter 设置）
        const auto user_id = request->attributes()->get<uint64_t>("user_id");

        // 3. 调用 Service 层更新分享设置
        auto result = co_await m_share_service->Update(*parse_result, user_id);
        if (!result) {
            LOG_ERROR << "Update share settings failed: " << result.error().message
                      << " (user_id=" << user_id << ", share_id=" << share_id << ")";
            co_return Response::Error(result.error());
        }

        // 4. 构造响应
        LOG_INFO << "Update share settings successful: share_id=" << share_id
                 << " (user_id=" << user_id << ")";
        co_return Response::Success(result->ToJson());
    }

    auto ShareController::Cancel(drogon::HttpRequestPtr request)
        -> drogon::Task<drogon::HttpResponsePtr> {

        LOG_INFO << "Received batch cancel shares request: " << request->getPeerAddr().toIpPort();

        // 1. 解析并验证请求参数
        auto parse_result = CancelShareRequest::FromRequest(request);
        if (!parse_result) {
            LOG_WARN << "Batch cancel shares request parameter validation failed: "
                     << parse_result.error().message;
            co_return Response::Error(parse_result.error());
        }
        LOG_DEBUG << "Batch cancel shares parameter validation passed: share_ids.size()="
                  << parse_result->share_ids.size();

        // 2. 从请求属性获取 user_id（由 JwtAuthFilter 设置）
        const auto user_id = request->attributes()->get<uint64_t>("user_id");

        // 3. 调用 Service 层批量取消分享
        auto result = co_await m_share_service->Cancel(*parse_result, user_id);
        if (!result) {
            LOG_ERROR << "Batch cancel shares failed: " << result.error().message
                      << " (user_id=" << user_id << ")";
            co_return Response::Error(result.error());
        }

        // 4. 构造响应（批量操作始终返回 200）
        LOG_INFO << "Batch cancel shares completed: total=" << result->summary.total
                 << ", succeeded=" << result->summary.succeeded
                 << ", failed=" << result->summary.failed << " (user_id=" << user_id << ")";
        co_return Response::Success(result->ToJson());
    }

    // ==================== 公开端点（无需认证） ====================

    auto ShareController::Access(drogon::HttpRequestPtr request, std::string share_id)
        -> drogon::Task<drogon::HttpResponsePtr> {

        const auto ip_address = request->getPeerAddr().toIp();
        LOG_INFO << "Received verify share access request: " << request->getPeerAddr().toIpPort()
                 << ", share_id=" << share_id;

        // 1. 解析并验证请求参数
        auto parse_result = AccessShareRequest::FromRequest(request, share_id);
        if (!parse_result) {
            LOG_WARN << "Verify share access request parameter validation failed: "
                     << parse_result.error().message;
            co_return Response::Error(parse_result.error());
        }
        LOG_DEBUG << "Verify share access parameter validation passed: share_id="
                  << parse_result->share_id
                  << ", has_password=" << parse_result->password.has_value();

        // 2. 调用 Service 层验证分享访问
        auto result = co_await m_share_service->Access(*parse_result, ip_address);
        if (!result) {
            LOG_WARN << "Verify share access failed: " << result.error().message
                     << " (share_id=" << share_id << ", ip=" << ip_address << ")";
            co_return Response::Error(result.error());
        }

        // 3. 构造响应
        LOG_INFO << "Verify share access successful: share_id=" << share_id
                 << ", permission=" << result->permission << " (ip=" << ip_address << ")";
        co_return Response::Success(result->ToJson());
    }

    // ==================== 分享令牌保护端点 ====================

    auto ShareController::Browse(drogon::HttpRequestPtr request, std::string share_id)
        -> drogon::Task<drogon::HttpResponsePtr> {

        LOG_INFO << "Received browse share content request: " << request->getPeerAddr().toIpPort()
                 << ", share_id=" << share_id;

        // 1. 解析并验证请求参数
        auto parse_result = BrowseShareRequest::FromRequest(request, share_id);
        if (!parse_result) {
            LOG_WARN << "Browse share content request parameter validation failed: "
                     << parse_result.error().message;
            co_return Response::Error(parse_result.error());
        }
        LOG_DEBUG << "Browse share content parameter validation passed: share_id="
                  << parse_result->share_id << ", folder_id="
                  << (parse_result->folder_id.has_value() ?
                          std::to_string(*parse_result->folder_id) :
                          "null");

        // 2. 从请求属性获取 share_id（由 ShareAuthFilter 设置）
        const auto internal_share_id = request->attributes()->get<uint64_t>("share_id");
        const auto& share_code = request->attributes()->get<std::string>("share_code");

        // 3. 验证 share_id 匹配（防止令牌用于其他分享）
        if (share_id != share_code) {
            LOG_WARN << "Share token does not match requested share_id: token_share_code="
                     << share_code << ", request_share_id=" << share_id;
            co_return Response::Error(ErrorInfo(
                ErrorCode::ShareAccessDenied,
                "Share token does not match requested share"
            ));
        }

        // 4. 调用 Service 层浏览分享内容
        auto result = co_await m_share_service->Browse(*parse_result, internal_share_id);
        if (!result) {
            LOG_ERROR << "Browse share content failed: " << result.error().message
                      << " (share_id=" << share_id << ")";
            co_return Response::Error(result.error());
        }

        // 5. 构造响应
        LOG_INFO << "Browse share content successful: share_id=" << share_id
                 << ", items=" << result->items.size()
                 << " (internal_share_id=" << internal_share_id << ")";
        co_return Response::Success(result->ToJson());
    }

    auto ShareController::Download(
        drogon::HttpRequestPtr request,
        std::string share_id,
        std::string file_id
    ) -> drogon::Task<drogon::HttpResponsePtr> {

        LOG_INFO << "Received download share file request: " << request->getPeerAddr().toIpPort()
                 << ", share_id=" << share_id << ", file_id=" << file_id;

        // 1. 解析并验证路径参数
        auto parse_result = DownloadShareRequest::FromPath(share_id, file_id);
        if (!parse_result) {
            LOG_WARN << "Download share file request parameter validation failed: "
                     << parse_result.error().message;
            co_return Response::Error(parse_result.error());
        }
        LOG_DEBUG << "Download share file parameter validation passed: share_id="
                  << parse_result->share_id << ", file_id=" << parse_result->file_id;

        // 2. 从请求属性获取 share_id（由 ShareAuthFilter 设置）
        const auto internal_share_id = request->attributes()->get<uint64_t>("share_id");
        const auto& share_code = request->attributes()->get<std::string>("share_code");

        // 3. 验证 share_id 匹配（防止令牌用于其他分享）
        if (share_id != share_code) {
            LOG_WARN << "Share token does not match requested share_id: token_share_code="
                     << share_code << ", request_share_id=" << share_id;
            co_return Response::Error(ErrorInfo(
                ErrorCode::ShareAccessDenied,
                "Share token does not match requested share"
            ));
        }

        // 4. 获取下载文件信息
        auto info_result =
            co_await m_share_service->GetDownloadInfo(*parse_result, internal_share_id);
        if (!info_result) {
            LOG_ERROR << "Get download info failed: " << info_result.error().message
                      << " (share_id=" << share_id << ", file_id=" << file_id << ")";
            co_return Response::Error(info_result.error());
        }

        const auto& download_info = *info_result;
        LOG_INFO << "Get download info successful: share_id=" << share_id << ", file_id=" << file_id
                 << ", filename=" << download_info.filename << ", size=" << download_info.file_size
                 << ", storage_path=" << download_info.storage_path;

        // 5. 检查文件是否存在
        if (!std::filesystem::exists(download_info.storage_path)) {
            LOG_ERROR << "File not found: " << download_info.storage_path;
            co_return Response::Error(ErrorInfo(ErrorCode::FileNotFound, "File not found"));
        }

        // 6. 委托共享下载响应构造
        auto resp = co_await BuildDownloadResponse(
            disk::controllers::DownloadParams{
                .storage_path = download_info.storage_path,
                .filename = download_info.filename,
                .file_size = download_info.file_size,
                .mime_type = download_info.mime_type,
                .file_hash = download_info.hash_md5,
                .range_header = std::string(request->getHeader("Range")),
            },
            m_storage
        );

        // 7. 增加下载次数
        co_await m_share_service->IncrementDownloadCount(internal_share_id);

        co_return resp;
    }

} // namespace disk::share
