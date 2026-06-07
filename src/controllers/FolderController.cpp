/**
 * @file FolderController.cpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 文件夹控制器
 *
 * @copyright Copyright (c) 2026
 *
 */

#include "FolderController.hpp"

#include "dtos/FolderDto.hpp"
#include "utils/Response.hpp"

namespace disk::folder {

    FolderController::FolderController()
        : m_folder_service(std::make_unique<FolderService>(drogon::app().getDbClient())) {
    }

    auto FolderController::CreateFolder(drogon::HttpRequestPtr request)
        -> drogon::Task<drogon::HttpResponsePtr> {

        Logger::Info() << "Received create folder request: " << request->getPeerAddr().toIpPort();

        // 1. 解析并验证请求参数
        auto parse_result = CreateFolderRequest::FromRequest(request);
        if (!parse_result) {
            Logger::Warn() << "Create folder request parameter validation failed: "
                     << parse_result.error().message;
            co_return Response::Error(parse_result.error());
        }
        Logger::Debug() << "Create folder parameter validation passed: name=\"" << parse_result->name
                  << "\", parent_id=" << parse_result->parent_id;

        // 2. 从请求属性获取 user_id（由 JwtAuthFilter 设置）
        const auto user_id = request->attributes()->get<uint64_t>("user_id");

        // 3. 调用 Service 层创建文件夹
        auto result = co_await m_folder_service->CreateFolder(*parse_result, user_id);
        if (!result) {
            Logger::Error() << "Create folder failed: " << result.error().message
                      << " (user_id=" << user_id << ")";
            co_return Response::Error(result.error());
        }

        // 4. 构造响应
        Logger::Info() << "Create folder successful: " << result->name << " (ID: " << result->id
                 << ", user_id: " << user_id << ")";
        co_return Response::Success(result->ToJson());
    }

    auto FolderController::GetTree(drogon::HttpRequestPtr request)
        -> drogon::Task<drogon::HttpResponsePtr> {

        Logger::Info() << "Received get folder tree request: " << request->getPeerAddr().toIpPort();

        // 1. 解析并验证请求参数
        auto parse_result = FolderTreeRequest::FromRequest(request);
        if (!parse_result) {
            Logger::Warn() << "Folder tree request parameter validation failed: "
                     << parse_result.error().message;
            co_return Response::Error(parse_result.error());
        }

        // 2. 从请求属性获取 user_id（由 JwtAuthFilter 设置）
        const auto user_id = request->attributes()->get<uint64_t>("user_id");

        // 3. 调用 Service 层获取文件夹树
        auto result = co_await m_folder_service
                          ->GetFolderTree(user_id, parse_result->parent_id, parse_result->depth);
        if (!result) {
            Logger::Error() << "Get folder tree failed: " << result.error().message
                      << " (user_id=" << user_id << ")";
            co_return Response::Error(result.error());
        }

        // 4. 返回成功响应
        Logger::Info() << "Get folder tree successful: user_id=" << user_id
                 << ", parent_id=" << parse_result->parent_id << ", depth=" << parse_result->depth;
        co_return Response::Success(result->ToJson());
    }

    auto FolderController::GetBreadcrumb(drogon::HttpRequestPtr request, const std::string& folder_id)
        -> drogon::Task<drogon::HttpResponsePtr> {

        Logger::Info() << "Received get breadcrumb request: " << request->getPeerAddr().toIpPort()
                 << ", folder_id=" << folder_id;

        // 1. 验证并解析 folder_id
        if (folder_id.empty()) {
            Logger::Warn() << "Missing required parameter: folder_id";
            co_return Response::Error(
                ErrorInfo(ErrorCode::InvalidParameter, "Missing required parameter: folder_id")
            );
        }

        // 检查是否为负数（stoull 会将负数回绕）
        if (folder_id[0] == '-') {
            Logger::Warn() << "Parameter 'folder_id' must be a positive integer: " << folder_id;
            co_return Response::Error(ErrorInfo(
                ErrorCode::InvalidParameter,
                "Parameter 'folder_id' must be a positive integer"
            ));
        }

        uint64_t parsed_folder_id = 0;
        try {
            size_t pos = 0;
            parsed_folder_id = std::stoull(folder_id, &pos);
            if (pos != folder_id.length() || parsed_folder_id == 0) {
                Logger::Warn() << "Parameter 'folder_id' must be a positive integer: " << folder_id;
                co_return Response::Error(ErrorInfo(
                    ErrorCode::InvalidParameter,
                    "Parameter 'folder_id' must be a positive integer"
                ));
            }
        } catch (const std::exception& e) {
            Logger::Warn() << "Parameter 'folder_id' invalid format: " << folder_id;
            co_return Response::Error(
                ErrorInfo(ErrorCode::InvalidParameter, "Parameter 'folder_id' invalid format")
            );
        }

        // 2. 从请求属性获取 user_id（由 JwtAuthFilter 设置）
        const auto user_id = request->attributes()->get<uint64_t>("user_id");

        // 3. 调用 Service 层获取面包屑
        auto result = co_await m_folder_service->GetBreadcrumb(parsed_folder_id, user_id);
        if (!result) {
            Logger::Error() << "Get breadcrumb failed: " << result.error().message
                      << " (user_id=" << user_id << ", folder_id=" << parsed_folder_id << ")";
            co_return Response::Error(result.error());
        }

        // 4. 返回成功响应
        Logger::Info() << "Get breadcrumb successful: user_id=" << user_id
                 << ", folder_id=" << parsed_folder_id << ", path_count=" << result->path.size();
        co_return Response::Success(result->ToJson());
    }

    auto FolderController::Rename(drogon::HttpRequestPtr request, const std::string& folder_id)
        -> drogon::Task<drogon::HttpResponsePtr> {

        Logger::Info() << "Received rename folder request: " << request->getPeerAddr().toIpPort()
                 << ", folder_id=" << folder_id;

        auto parse_result = RenameFolderRequest::FromPathAndRequest(folder_id, request);
        if (!parse_result) {
            Logger::Warn() << "Rename folder request parameter validation failed: "
                     << parse_result.error().message;
            co_return Response::Error(parse_result.error());
        }

        const auto user_id = request->attributes()->get<uint64_t>("user_id");
        auto result = co_await m_folder_service
                          ->Rename(parse_result->folder_id, parse_result->new_name, user_id);
        if (!result) {
            Logger::Error() << "Rename folder failed: " << result.error().message
                      << " (user_id=" << user_id << ", folder_id=" << parse_result->folder_id
                      << ")";
            co_return Response::Error(result.error());
        }

        Logger::Info() << "Rename folder successful: folder_id=" << result->id << ", name="
                 << result->name << ", user_id=" << user_id;
        co_return Response::Success(result->ToJson());
    }

} // namespace disk::folder
