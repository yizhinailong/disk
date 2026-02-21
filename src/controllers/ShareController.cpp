/**
 * @file ShareController.cpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 分享控制器
 * @version 0.1
 * @date 2026-02-15
 *
 * @copyright Copyright (c) 2026
 *
 */

#include "ShareController.hpp"

#include <filesystem>
#include <fstream>

#include "dtos/ShareDto.hpp"
#include "utils/ConfigMgr.hpp"
#include "utils/Response.hpp"

namespace disk::share {

    using disk::utils::ConfigMgr;

    ShareController::ShareController()
        : m_share_service(
              std::make_unique<ShareService>(
                  drogon::app().getDbClient(),
                  drogon::app().getRedisClient(),
                  ConfigMgr::GetInstance()->GetJwtSecret()
              )
          ) {
        LOG_DEBUG << "ShareController initialized";
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
        LOG_INFO << "获取分享详情成功: share_id=" << share_id << ", files=" << result->files.size()
                 << " (user_id=" << user_id << ")";
        co_return Response::Success(result->ToJson());
    }

    auto ShareController::Update(drogon::HttpRequestPtr request, std::string share_id)
        -> drogon::Task<drogon::HttpResponsePtr> {

        LOG_INFO << "收到更新分享设置请求: " << request->getPeerAddr().toIpPort()
                 << ", share_id=" << share_id;

        // 1. 解析并验证请求参数
        auto parse_result = UpdateShareRequest::FromRequest(request, share_id);
        if (!parse_result) {
            LOG_WARN << "更新分享设置请求参数验证失败: " << parse_result.error().message;
            co_return Response::Error(parse_result.error());
        }
        LOG_DEBUG << "更新分享设置参数验证通过: share_id=" << parse_result->share_id
                  << ", expire_days="
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
            LOG_ERROR << "更新分享设置失败: " << result.error().message << " (user_id=" << user_id
                      << ", share_id=" << share_id << ")";
            co_return Response::Error(result.error());
        }

        // 4. 构造响应
        LOG_INFO << "更新分享设置成功: share_id=" << share_id << " (user_id=" << user_id << ")";
        co_return Response::Success(result->ToJson());
    }

    auto ShareController::Cancel(drogon::HttpRequestPtr request)
        -> drogon::Task<drogon::HttpResponsePtr> {

        LOG_INFO << "收到批量取消分享请求: " << request->getPeerAddr().toIpPort();

        // 1. 解析并验证请求参数
        auto parse_result = CancelShareRequest::FromRequest(request);
        if (!parse_result) {
            LOG_WARN << "批量取消分享请求参数验证失败: " << parse_result.error().message;
            co_return Response::Error(parse_result.error());
        }
        LOG_DEBUG << "批量取消分享参数验证通过: share_ids.size()="
                  << parse_result->share_ids.size();

        // 2. 从请求属性获取 user_id（由 JwtAuthFilter 设置）
        const auto user_id = request->attributes()->get<uint64_t>("user_id");

        // 3. 调用 Service 层批量取消分享
        auto result = co_await m_share_service->Cancel(*parse_result, user_id);
        if (!result) {
            LOG_ERROR << "批量取消分享失败: " << result.error().message << " (user_id=" << user_id
                      << ")";
            co_return Response::Error(result.error());
        }

        // 4. 构造响应（批量操作始终返回 200）
        LOG_INFO << "批量取消分享完成: total=" << result->summary.total
                 << ", succeeded=" << result->summary.succeeded
                 << ", failed=" << result->summary.failed << " (user_id=" << user_id << ")";
        co_return Response::Success(result->ToJson());
    }

    // ==================== 公开端点（无需认证） ====================

    auto ShareController::Access(drogon::HttpRequestPtr request, std::string share_id)
        -> drogon::Task<drogon::HttpResponsePtr> {

        const auto ip_address = request->getPeerAddr().toIp();
        LOG_INFO << "收到验证分享访问请求: " << request->getPeerAddr().toIpPort()
                 << ", share_id=" << share_id;

        // 1. 解析并验证请求参数
        auto parse_result = AccessShareRequest::FromRequest(request, share_id);
        if (!parse_result) {
            LOG_WARN << "验证分享访问请求参数验证失败: " << parse_result.error().message;
            co_return Response::Error(parse_result.error());
        }
        LOG_DEBUG << "验证分享访问参数验证通过: share_id=" << parse_result->share_id
                  << ", has_password=" << parse_result->password.has_value();

        // 2. 调用 Service 层验证分享访问
        auto result = co_await m_share_service->Access(*parse_result, ip_address);
        if (!result) {
            LOG_WARN << "验证分享访问失败: " << result.error().message << " (share_id=" << share_id
                     << ", ip=" << ip_address << ")";
            co_return Response::Error(result.error());
        }

        // 3. 构造响应
        LOG_INFO << "验证分享访问成功: share_id=" << share_id
                 << ", permission=" << result->permission << " (ip=" << ip_address << ")";
        co_return Response::Success(result->ToJson());
    }

    // ==================== 分享令牌保护端点 ====================

    auto ShareController::Browse(drogon::HttpRequestPtr request, std::string share_id)
        -> drogon::Task<drogon::HttpResponsePtr> {

        LOG_INFO << "收到浏览分享内容请求: " << request->getPeerAddr().toIpPort()
                 << ", share_id=" << share_id;

        // 1. 解析并验证请求参数
        auto parse_result = BrowseShareRequest::FromRequest(request, share_id);
        if (!parse_result) {
            LOG_WARN << "浏览分享内容请求参数验证失败: " << parse_result.error().message;
            co_return Response::Error(parse_result.error());
        }
        LOG_DEBUG << "浏览分享内容参数验证通过: share_id=" << parse_result->share_id
                  << ", folder_id="
                  << (parse_result->folder_id.has_value() ?
                          std::to_string(*parse_result->folder_id) :
                          "null");

        // 2. 从请求属性获取 share_id（由 ShareAuthFilter 设置）
        const auto internal_share_id = request->attributes()->get<uint64_t>("share_id");
        const auto& share_code = request->attributes()->get<std::string>("share_code");

        // 3. 验证 share_id 匹配（防止令牌用于其他分享）
        if (share_id != share_code) {
            LOG_WARN << "分享令牌与请求的 share_id 不匹配: token_share_code=" << share_code
                     << ", request_share_id=" << share_id;
            co_return Response::Error(
                ErrorInfo(ErrorCode::ShareAccessDenied, "分享令牌与请求的分享不匹配")
            );
        }

        // 4. 调用 Service 层浏览分享内容
        auto result = co_await m_share_service->Browse(*parse_result, internal_share_id);
        if (!result) {
            LOG_ERROR << "浏览分享内容失败: " << result.error().message << " (share_id=" << share_id
                      << ")";
            co_return Response::Error(result.error());
        }

        // 5. 构造响应
        LOG_INFO << "浏览分享内容成功: share_id=" << share_id << ", items=" << result->items.size()
                 << " (internal_share_id=" << internal_share_id << ")";
        co_return Response::Success(result->ToJson());
    }

    auto ShareController::Download(
        drogon::HttpRequestPtr request,
        std::string share_id,
        std::string file_id
    ) -> drogon::Task<drogon::HttpResponsePtr> {

        LOG_INFO << "收到下载分享文件请求: " << request->getPeerAddr().toIpPort()
                 << ", share_id=" << share_id << ", file_id=" << file_id;

        // 1. 解析并验证路径参数
        auto parse_result = DownloadShareRequest::FromPath(share_id, file_id);
        if (!parse_result) {
            LOG_WARN << "下载分享文件请求参数验证失败: " << parse_result.error().message;
            co_return Response::Error(parse_result.error());
        }
        LOG_DEBUG << "下载分享文件参数验证通过: share_id=" << parse_result->share_id
                  << ", file_id=" << parse_result->file_id;

        // 2. 从请求属性获取 share_id（由 ShareAuthFilter 设置）
        const auto internal_share_id = request->attributes()->get<uint64_t>("share_id");
        const auto& share_code = request->attributes()->get<std::string>("share_code");

        // 3. 验证 share_id 匹配（防止令牌用于其他分享）
        if (share_id != share_code) {
            LOG_WARN << "分享令牌与请求的 share_id 不匹配: token_share_code=" << share_code
                     << ", request_share_id=" << share_id;
            co_return Response::Error(
                ErrorInfo(ErrorCode::ShareAccessDenied, "分享令牌与请求的分享不匹配")
            );
        }

        // 4. 获取下载文件信息
        auto info_result =
            co_await m_share_service->GetDownloadInfo(*parse_result, internal_share_id);
        if (!info_result) {
            LOG_ERROR << "获取下载信息失败: " << info_result.error().message
                      << " (share_id=" << share_id << ", file_id=" << file_id << ")";
            co_return Response::Error(info_result.error());
        }

        const auto& download_info = *info_result;
        LOG_INFO << "获取下载信息成功: share_id=" << share_id << ", file_id=" << file_id
                 << ", filename=" << download_info.filename << ", size=" << download_info.file_size
                 << ", storage_path=" << download_info.storage_path;

        // 5. 检查文件是否存在
        if (!std::filesystem::exists(download_info.storage_path)) {
            LOG_ERROR << "文件不存在: " << download_info.storage_path;
            co_return Response::Error(ErrorInfo(ErrorCode::FileNotFound, "文件不存在"));
        }

        // 6. 解析 Range 请求头
        auto range_header = std::string(request->getHeader("Range"));
        auto range_request = RangeRequest::Parse(range_header, download_info.file_size);

        // 7. 处理 Range 请求
        if (range_request.has_range && !range_request.satisfiable) {
            LOG_WARN << "Range 请求无法满足: " << range_header
                     << ", file_size=" << download_info.file_size;

            auto resp = drogon::HttpResponse::newHttpResponse();
            resp->setStatusCode(drogon::HttpStatusCode::k416RequestedRangeNotSatisfiable);
            resp->setContentTypeCode(drogon::CT_APPLICATION_JSON);

            std::string content_range = "bytes */" + std::to_string(download_info.file_size);
            resp->addHeader("Content-Range", content_range);

            Json::Value error_data;
            error_data["file_size"] = static_cast<Json::UInt64>(download_info.file_size);
            error_data["requested_range"] = range_header;
            error_data["reason"] = "请求的起始位置超出文件大小";

            Json::Value body;
            body["code"] = 10002;
            body["message"] = "请求范围无效";
            body["data"] = error_data;

            resp->setBody(body.toStyledString());
            co_return resp;
        }

        // 8. 读取文件内容
        std::ifstream file(download_info.storage_path, std::ios::binary);
        if (!file.is_open()) {
            LOG_ERROR << "无法打开文件: " << download_info.storage_path;
            co_return Response::Error(ErrorInfo(ErrorCode::FileNotFound, "无法打开文件"));
        }

        uint64_t start = range_request.has_range ? range_request.start : 0;
        uint64_t end = range_request.has_range ? range_request.end : download_info.file_size - 1;
        uint64_t content_length = end - start + 1;

        file.seekg(static_cast<std::streampos>(start));
        std::string content(content_length, '\0');
        file.read(content.data(), static_cast<std::streamsize>(content_length));
        file.close();

        // 9. 构建响应
        auto resp = drogon::HttpResponse::newHttpResponse();

        if (range_request.has_range) {
            resp->setStatusCode(drogon::HttpStatusCode::k206PartialContent);
            std::string content_range = "bytes " + std::to_string(start) + "-" +
                                        std::to_string(end) + "/" +
                                        std::to_string(download_info.file_size);
            resp->addHeader("Content-Range", content_range);
            LOG_INFO << "返回部分内容: start=" << start << ", end=" << end
                     << ", total=" << download_info.file_size;
        } else {
            resp->setStatusCode(drogon::HttpStatusCode::k200OK);
            LOG_INFO << "返回完整文件: size=" << download_info.file_size;
        }

        // 设置响应头
        resp->setContentTypeString(download_info.mime_type);
        resp->addHeader("Content-Length", std::to_string(content_length));
        resp->addHeader("Accept-Ranges", "bytes");

        std::string disposition = "attachment; filename=\"" + download_info.filename + "\"";
        resp->addHeader("Content-Disposition", disposition);

        if (!download_info.hash_md5.empty()) {
            resp->addHeader("ETag", "\"" + download_info.hash_md5 + "\"");
        }

        resp->setBody(std::move(content));

        // 10. 增加下载次数
        co_await m_share_service->IncrementDownloadCount(internal_share_id);

        co_return resp;
    }

} // namespace disk::share
