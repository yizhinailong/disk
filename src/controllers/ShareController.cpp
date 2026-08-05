/**
 * @file ShareController.cpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 分享控制器
 *
 * @copyright Copyright (c) 2026
 *
 */

#include "ShareController.hpp"

#include <charconv>

#include "DownloadResponder.hpp"
#include "application/ApplicationContext.hpp"
#include "controllers/ControllerHelpers.hpp"
#include "dtos/ShareDto.hpp"
#include "utils/ClientIp.hpp"
#include "utils/Response.hpp"

namespace disk::share {

    namespace {
        auto BuildAuditContext(const drogon::HttpRequestPtr& request) -> ShareAuditContext {
            return ShareAuditContext{
                .ip_address = disk::utils::ResolveClientIp(request),
                .user_agent = std::string(request->getHeader("User-Agent")),
            };
        }

        auto IsSuccessfulContentDownload(const drogon::HttpResponsePtr& resp) -> bool {
            if (!resp) {
                return false;
            }
            const auto status = resp->getStatusCode();
            return status == drogon::HttpStatusCode::k200OK ||
                   status == drogon::HttpStatusCode::k206PartialContent;
        }

        auto ParseContentLength(const drogon::HttpResponsePtr& response) -> uint64_t {
            const auto value = response->getHeader("Content-Length");
            uint64_t bytes = 0;
            const auto [end, error] = std::from_chars(
                value.data(),
                value.data() + value.size(),
                bytes
            );
            return error == std::errc{} && end == value.data() + value.size() ? bytes : 0;
        }

        auto BuildDownloadOutcome(
            const drogon::HttpResponsePtr& response,
            uint64_t file_size
        ) -> ShareDownloadOutcome {
            ShareDownloadOutcome outcome;
            if (!response) {
                outcome.result = "response_unavailable";
                outcome.update_statistics = true;
                return outcome;
            }

            const auto status = response->getStatusCode();
            outcome.http_status = static_cast<int>(status);
            outcome.success = IsSuccessfulContentDownload(response);
            outcome.update_statistics = true;
            if (outcome.success) {
                outcome.bytes = ParseContentLength(response);
                if (outcome.bytes == 0 && status == drogon::HttpStatusCode::k200OK) {
                    outcome.bytes = file_size;
                }
            }

            if (status == drogon::HttpStatusCode::k200OK) {
                outcome.result = "success";
            } else if (status == drogon::HttpStatusCode::k206PartialContent) {
                outcome.result = "partial_content";
            } else if (status == drogon::HttpStatusCode::k416RequestedRangeNotSatisfiable) {
                outcome.result = "range_not_satisfiable";
            } else if (status == drogon::HttpStatusCode::k404NotFound) {
                outcome.result = "not_found";
            } else {
                outcome.result = "failed";
            }
            return outcome;
        }

        auto DownloadFailureResult(ErrorCode code) -> std::string {
            switch (code) {
                case ErrorCode::ShareNotFound    : return "share_not_found";
                case ErrorCode::ShareExpired     : return "share_inactive";
                case ErrorCode::ShareAccessDenied: return "access_denied";
                case ErrorCode::FileNotFound     : return "file_not_found";
                default                          : return "internal_error";
            }
        }
    } // namespace

    ShareController::ShareController()
        : m_share_service(&disk::application::ApplicationContext::GetInstance()->Share()),
          m_blob_store(disk::application::ApplicationContext::GetInstance()->BlobStore()),
          m_download_integrity_service(
              disk::application::ApplicationContext::GetInstance()->DownloadIntegrity()
          ) {
    }

    /// ==================== 所有者端点（JWT 保护） ====================

    auto ShareController::Create(drogon::HttpRequestPtr request)
        -> drogon::Task<drogon::HttpResponsePtr> {
        auto log_context = disk::controllers::GetRequestLogContext(request, "share");

        Logger::Info(log_context) << "Received create share request";

        /// 1. 解析并验证请求参数
        auto parse_result = CreateShareRequest::FromRequest(request, log_context);
        if (!parse_result) {
            Logger::Warn(log_context) << "Create share request parameter validation failed";
            co_return Response::Error(parse_result.error());
        }
        Logger::Debug(log_context) << "Create share parameter validation passed";

        /// 2. 从请求属性获取 user_id（由 JwtAuthFilter 设置）
        const auto user_id = disk::controllers::GetAuthenticatedUserId(request);

        /// 3. 调用 Service 层创建分享
        auto result = co_await m_share_service->Create(
            *parse_result,
            user_id,
            BuildAuditContext(request),
            log_context
        );
        if (!result) {
            Logger::Error(log_context) << "Create share failed";
            co_return Response::Error(result.error());
        }

        /// 4. 构造响应
        Logger::Info(log_context) << "Create share successful";
        co_return Response::Success(result->ToJson());
    }

    auto ShareController::List(drogon::HttpRequestPtr request)
        -> drogon::Task<drogon::HttpResponsePtr> {
        auto log_context = disk::controllers::GetRequestLogContext(request, "share");

        Logger::Info(log_context)
            << "Received get share list request: " << request->getPeerAddr().toIpPort();

        /// 1. 解析并验证请求参数
        auto parse_result = ShareListRequest::FromRequest(request, log_context);
        if (!parse_result) {
            Logger::Warn(log_context)
                << "Share list request parameter validation failed: "
                << parse_result.error().message;
            co_return Response::Error(parse_result.error());
        }
        Logger::Debug(log_context)
            << "Share list parameter validation passed: status=" << parse_result->status
            << ", page=" << parse_result->page << ", page_size=" << parse_result->page_size;

        /// 2. 从请求属性获取 user_id（由 JwtAuthFilter 设置）
        const auto user_id = disk::controllers::GetAuthenticatedUserId(request);

        /// 3. 调用 Service 层获取分享列表
        auto result = co_await m_share_service->List(*parse_result, user_id, log_context);
        if (!result) {
            Logger::Error(log_context)
                << "Get share list failed: " << result.error().message
                << " (user_id=" << user_id << ")";
            co_return Response::Error(result.error());
        }

        /// 4. 构造响应
        Logger::Info(log_context)
            << "Get share list successful: items=" << result->items.size()
            << ", total=" << result->pagination.total << " (user_id=" << user_id << ")";
        co_return Response::Success(result->ToJson());
    }

    auto ShareController::Detail(drogon::HttpRequestPtr request, std::string share_id)
        -> drogon::Task<drogon::HttpResponsePtr> {
        auto log_context = disk::controllers::GetRequestLogContext(request, "share");

        Logger::Info(log_context)
            << "Received get share details request: " << request->getPeerAddr().toIpPort()
            << ", share_id=" << share_id;

        /// 1. 解析并验证路径参数
        auto parse_result = ShareDetailRequest::FromPath(share_id, log_context);
        if (!parse_result) {
            Logger::Warn(log_context)
                << "Share detail request parameter validation failed: "
                << parse_result.error().message;
            co_return Response::Error(parse_result.error());
        }

        /// 2. 从请求属性获取 user_id（由 JwtAuthFilter 设置）
        const auto user_id = disk::controllers::GetAuthenticatedUserId(request);

        /// 3. 调用 Service 层获取分享详情
        auto result = co_await m_share_service->Detail(*parse_result, user_id, log_context);
        if (!result) {
            Logger::Error(log_context)
                << "Get share details failed: " << result.error().message
                << " (user_id=" << user_id << ", share_id=" << share_id << ")";
            co_return Response::Error(result.error());
        }

        /// 4. 构造响应
        Logger::Info(log_context)
            << "Get share details successful: share_id=" << share_id
            << ", files=" << result->files.size() << " (user_id=" << user_id << ")";
        co_return Response::Success(result->ToJson());
    }

    auto ShareController::Update(drogon::HttpRequestPtr request, std::string share_id)
        -> drogon::Task<drogon::HttpResponsePtr> {
        auto log_context = disk::controllers::GetRequestLogContext(request, "share");

        Logger::Info(log_context)
            << "Received update share settings request: " << request->getPeerAddr().toIpPort()
            << ", share_id=" << share_id;

        /// 1. 解析并验证请求参数
        auto parse_result = UpdateShareRequest::FromRequest(request, share_id, log_context);
        if (!parse_result) {
            Logger::Warn(log_context)
                << "Update share settings request parameter validation failed: "
                << parse_result.error().message;
            co_return Response::Error(parse_result.error());
        }
        Logger::Debug(log_context)
            << "Update share settings parameter validation passed: share_id="
            << parse_result->share_id << ", expire_days="
            << (parse_result->expire_days.has_value() ?
                    std::to_string(*parse_result->expire_days) :
                    "null")
            << ", password=" << (parse_result->password.has_value() ? "set" : "null")
            << ", permission="
            << (parse_result->permission.has_value() ?
                    SharePermissionToString(*parse_result->permission) :
                    "null");

        /// 2. 从请求属性获取 user_id（由 JwtAuthFilter 设置）
        const auto user_id = disk::controllers::GetAuthenticatedUserId(request);

        /// 3. 调用 Service 层更新分享设置
        auto result = co_await m_share_service->Update(*parse_result, user_id, log_context);
        if (!result) {
            Logger::Error(log_context)
                << "Update share settings failed: " << result.error().message
                << " (user_id=" << user_id << ", share_id=" << share_id << ")";
            co_return Response::Error(result.error());
        }

        /// 4. 构造响应
        Logger::Info(log_context)
            << "Update share settings successful: share_id=" << share_id
            << " (user_id=" << user_id << ")";
        co_return Response::Success(result->ToJson());
    }

    auto ShareController::Cancel(drogon::HttpRequestPtr request)
        -> drogon::Task<drogon::HttpResponsePtr> {
        auto log_context = disk::controllers::GetRequestLogContext(request, "share");

        Logger::Info(log_context)
            << "Received batch cancel shares request: " << request->getPeerAddr().toIpPort();

        /// 1. 解析并验证请求参数
        auto parse_result = CancelShareRequest::FromRequest(request, log_context);
        if (!parse_result) {
            Logger::Warn(log_context)
                << "Batch cancel shares request parameter validation failed: "
                << parse_result.error().message;
            co_return Response::Error(parse_result.error());
        }
        Logger::Debug(log_context)
            << "Batch cancel shares parameter validation passed: share_ids.size()="
            << parse_result->share_ids.size();

        /// 2. 从请求属性获取 user_id（由 JwtAuthFilter 设置）
        const auto user_id = disk::controllers::GetAuthenticatedUserId(request);

        /// 3. 调用 Service 层批量取消分享
        auto result = co_await m_share_service->Cancel(
            *parse_result,
            user_id,
            BuildAuditContext(request),
            log_context
        );
        if (!result) {
            Logger::Error(log_context)
                << "Batch cancel shares failed: " << result.error().message
                << " (user_id=" << user_id << ")";
            co_return Response::Error(result.error());
        }

        /// 4. 构造响应（批量操作始终返回 200）
        Logger::Info(log_context)
            << "Batch cancel shares completed: total=" << result->summary.total
            << ", succeeded=" << result->summary.succeeded
            << ", failed=" << result->summary.failed << " (user_id=" << user_id << ")";
        co_return Response::Success(result->ToJson());
    }

    /// ==================== 公开端点（无需认证） ====================

    auto ShareController::Access(drogon::HttpRequestPtr request, std::string share_id)
        -> drogon::Task<drogon::HttpResponsePtr> {
        auto log_context = disk::controllers::GetRequestLogContext(request, "share");

        const auto audit_context = BuildAuditContext(request);
        const auto& ip_address = audit_context.ip_address;
        Logger::Info(log_context)
            << "Received verify share access request: " << request->getPeerAddr().toIpPort()
            << ", share_id=" << share_id;

        /// 1. 解析并验证请求参数
        auto parse_result = AccessShareRequest::FromRequest(request, share_id, log_context);
        if (!parse_result) {
            Logger::Warn(log_context)
                << "Verify share access request parameter validation failed: "
                << parse_result.error().message;
            co_return Response::Error(parse_result.error());
        }
        Logger::Debug(log_context)
            << "Verify share access parameter validation passed: share_id="
            << parse_result->share_id
            << ", has_password=" << parse_result->password.has_value();

        /// 2. 调用 Service 层验证分享访问
        auto result =
            co_await m_share_service->Access(*parse_result, audit_context, log_context);
        if (!result) {
            Logger::Warn(log_context)
                << "Verify share access failed: " << result.error().message
                << " (share_id=" << share_id << ", ip=" << ip_address << ")";
            co_return Response::Error(result.error());
        }

        /// 3. 构造响应
        Logger::Info(log_context)
            << "Verify share access successful: share_id=" << share_id
            << ", permission=" << result->permission << " (ip=" << ip_address << ")";
        co_return Response::Success(result->ToJson());
    }

    /// ==================== 分享令牌保护端点 ====================

    auto ShareController::Browse(drogon::HttpRequestPtr request, std::string share_id)
        -> drogon::Task<drogon::HttpResponsePtr> {
        auto log_context = disk::controllers::GetRequestLogContext(request, "share");

        Logger::Info(log_context)
            << "Received browse share content request: " << request->getPeerAddr().toIpPort()
            << ", share_id=" << share_id;

        /// 1. 解析并验证请求参数
        auto parse_result = BrowseShareRequest::FromRequest(request, share_id, log_context);
        if (!parse_result) {
            Logger::Warn(log_context)
                << "Browse share content request parameter validation failed: "
                << parse_result.error().message;
            co_return Response::Error(parse_result.error());
        }
        Logger::Debug(log_context)
            << "Browse share content parameter validation passed: share_id="
            << parse_result->share_id << ", folder_id="
            << (parse_result->folder_id.has_value() ?
                    std::to_string(*parse_result->folder_id) :
                    "null");

        /// 2. 从请求属性获取 share_id（由 ShareAuthFilter 设置）
        const auto internal_share_id = request->attributes()->get<uint64_t>("share_id");
        const auto& share_code = request->attributes()->get<std::string>("share_code");

        /// 3. 验证 share_id 匹配（防止令牌用于其他分享）
        if (share_id != share_code) {
            Logger::Warn(log_context)
                << "Share token does not match requested share_id: token_share_code="
                << share_code << ", request_share_id=" << share_id;
            co_return Response::Error(ErrorInfo(
                ErrorCode::ShareAccessDenied,
                "Share token does not match requested share"
            ));
        }

        /// 4. 调用 Service 层浏览分享内容
        auto result =
            co_await m_share_service->Browse(*parse_result, internal_share_id, log_context);
        if (!result) {
            Logger::Error(log_context)
                << "Browse share content failed: " << result.error().message
                << " (share_id=" << share_id << ")";
            co_return Response::Error(result.error());
        }

        /// 5. 构造响应
        Logger::Info(log_context)
            << "Browse share content successful: share_id=" << share_id
            << ", items=" << result->items.size()
            << " (internal_share_id=" << internal_share_id << ")";
        co_return Response::Success(result->ToJson());
    }

    auto ShareController::DownloadInfo(
        drogon::HttpRequestPtr request,
        std::string share_id,
        std::string file_id
    ) -> drogon::Task<drogon::HttpResponsePtr> {
        auto log_context = disk::controllers::GetRequestLogContext(request, "download");

        Logger::Info(log_context)
            << "Received share download info request: " << request->getPeerAddr().toIpPort()
            << ", share_id=" << share_id << ", file_id=" << file_id;

        auto parse_result = DownloadShareRequest::FromPath(share_id, file_id, log_context);
        if (!parse_result) {
            Logger::Warn(log_context)
                << "Share download info request parameter validation failed: "
                << parse_result.error().message;
            co_return Response::Error(parse_result.error());
        }

        const auto internal_share_id = request->attributes()->get<uint64_t>("share_id");
        const auto& share_code = request->attributes()->get<std::string>("share_code");

        if (share_id != share_code) {
            Logger::Warn(log_context)
                << "Share token does not match requested share_id: token_share_code="
                << share_code << ", request_share_id=" << share_id;
            co_return Response::Error(ErrorInfo(
                ErrorCode::ShareAccessDenied,
                "Share token does not match requested share"
            ));
        }

        auto info_result = co_await m_share_service->GetDownloadInfo(
            *parse_result,
            internal_share_id,
            log_context
        );
        if (!info_result) {
            Logger::Error(log_context)
                << "Get share download info failed: " << info_result.error().message
                << " (share_id=" << share_id << ", file_id=" << file_id << ")";
            co_return Response::Error(info_result.error());
        }

        const auto& info = *info_result;
        disk::file::DownloadInfoResponse response;
        response.file_id = info.file_id;
        response.filename = info.filename;
        response.file_size = info.file_size;
        response.file_hash = info.file_hash;
        response.mime_type = info.mime_type;
        response.supports_range = info.supports_range;

        Logger::Info(log_context)
            << "Share download info successful: share_id=" << share_id
            << ", file_id=" << file_id << ", size=" << info.file_size;
        co_return Response::Success(response.ToJson());
    }

    auto ShareController::Download(
        drogon::HttpRequestPtr request,
        std::string share_id,
        std::string file_id
    ) -> drogon::Task<drogon::HttpResponsePtr> {
        auto log_context = disk::controllers::GetRequestLogContext(request, "download");

        Logger::Info(log_context)
            << "Received download share file request: " << request->getPeerAddr().toIpPort()
            << ", share_id=" << share_id << ", file_id=" << file_id;

        /// 1. 解析并验证路径参数
        auto parse_result = DownloadShareRequest::FromPath(share_id, file_id, log_context);
        if (!parse_result) {
            Logger::Warn(log_context)
                << "Download share file request parameter validation failed: "
                << parse_result.error().message;
            co_return Response::Error(parse_result.error());
        }
        Logger::Debug(log_context)
            << "Download share file parameter validation passed: share_id="
            << parse_result->share_id << ", file_id=" << parse_result->file_id;

        /// 2. 从请求属性获取 share_id（由 ShareAuthFilter 设置）
        const auto internal_share_id = request->attributes()->get<uint64_t>("share_id");
        const auto& share_code = request->attributes()->get<std::string>("share_code");

        /// 3. 验证 share_id 匹配（防止令牌用于其他分享）
        if (share_id != share_code) {
            Logger::Warn(log_context)
                << "Share token does not match requested share_id: token_share_code="
                << share_code << ", request_share_id=" << share_id;
            co_return Response::Error(ErrorInfo(
                ErrorCode::ShareAccessDenied,
                "Share token does not match requested share"
            ));
        }

        /// 4. 获取下载文件信息
        const auto audit_context = BuildAuditContext(request);
        auto info_result = co_await m_share_service->GetDownloadInfo(
            *parse_result,
            internal_share_id,
            log_context
        );
        if (!info_result) {
            Logger::Error(log_context)
                << "Get download info failed: " << info_result.error().message
                << " (share_id=" << share_id << ", file_id=" << file_id << ")";
            auto error_response = Response::Error(info_result.error());
            co_await m_share_service->CompleteDownload(
                *parse_result,
                internal_share_id,
                ShareDownloadOutcome{
                    .bytes = 0,
                    .http_status = static_cast<int>(error_response->getStatusCode()),
                    .success = false,
                    .update_statistics = false,
                    .result = DownloadFailureResult(info_result.error().code),
                },
                audit_context,
                log_context
            );
            co_return error_response;
        }

        const auto& download_info = *info_result;
        Logger::Info(log_context)
            << "Get download info successful: share_id=" << share_id
            << ", file_id=" << file_id
            << ", filename=" << download_info.filename
            << ", size=" << download_info.file_size
            << ", content_id=" << download_info.blob.content_id;

        /// 5. 委托共享下载响应构造
        auto resp = co_await BuildDownloadResponse(
            disk::controllers::DownloadParams{
                .blob = download_info.blob,
                .filename = download_info.filename,
                .file_size = download_info.file_size,
                .mime_type = download_info.mime_type,
                .file_hash = download_info.file_hash,
                .range_header = std::string(request->getHeader("Range")),
            },
            m_blob_store,
            m_download_integrity_service,
            log_context
        );

        /// 6. 统一更新下载统计并记录审计结果
        co_await m_share_service->CompleteDownload(
            *parse_result,
            internal_share_id,
            BuildDownloadOutcome(resp, download_info.file_size),
            audit_context,
            log_context
        );

        co_return resp;
    }

    auto ShareController::Save(drogon::HttpRequestPtr request, std::string share_id)
        -> drogon::Task<drogon::HttpResponsePtr> {
        auto log_context = disk::controllers::GetRequestLogContext(request, "share");

        Logger::Info(log_context)
            << "Received save share items request: " << request->getPeerAddr().toIpPort()
            << ", share_id=" << share_id;

        auto parse_result = SaveShareItemsRequest::FromRequest(request, share_id, log_context);
        if (!parse_result) {
            Logger::Warn(log_context)
                << "Save share items request parameter validation failed: "
                << parse_result.error().message;
            co_return Response::Error(parse_result.error());
        }

        const auto target_user_id = disk::controllers::GetAuthenticatedUserId(request);
        const auto internal_share_id = request->attributes()->get<uint64_t>("share_id");
        const auto& share_code = request->attributes()->get<std::string>("share_code");

        if (share_id != share_code) {
            Logger::Warn(log_context)
                << "Share token does not match requested share_id: token_share_code="
                << share_code << ", request_share_id=" << share_id;
            co_return Response::Error(ErrorInfo(
                ErrorCode::ShareAccessDenied,
                "Share token does not match requested share"
            ));
        }

        auto result = co_await m_share_service->SaveToDrive(
            *parse_result,
            internal_share_id,
            target_user_id,
            log_context
        );
        if (!result) {
            Logger::Error(log_context)
                << "Save share items failed: " << result.error().message
                << " (share_id=" << share_id << ", user_id=" << target_user_id << ")";
            co_return Response::Error(result.error());
        }

        Logger::Info(log_context)
            << "Save share items successful: saved_count=" << result->saved_count
            << " (share_id=" << share_id << ", user_id=" << target_user_id << ")";
        co_return Response::Success(result->ToJson());
    }

} // namespace disk::share
