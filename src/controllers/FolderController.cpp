/**
 * @file FolderController.cpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 文件夹控制器
 * @version 0.1
 * @date 2026-02-14
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
        LOG_DEBUG << "FolderController 初始化完成";
    }

    auto FolderController::CreateFolder(drogon::HttpRequestPtr request)
        -> drogon::Task<drogon::HttpResponsePtr> {

        LOG_INFO << "收到创建文件夹请求: " << request->getPeerAddr().toIpPort();

        // 1. 解析并验证请求参数
        auto parse_result = CreateFolderRequest::FromRequest(request);
        if (!parse_result) {
            LOG_WARN << "创建文件夹请求参数验证失败: " << parse_result.error().message;
            co_return Response::Error(parse_result.error());
        }
        LOG_DEBUG << "创建文件夹参数验证通过: name=\"" << parse_result->name
                  << "\", parent_id=" << parse_result->parent_id;

        // 2. 从请求属性获取 user_id（由 JwtAuthFilter 设置）
        const auto user_id = request->attributes()->get<uint64_t>("user_id");

        // 3. 调用 Service 层创建文件夹
        auto result = co_await m_folder_service->CreateFolder(*parse_result, user_id);
        if (!result) {
            LOG_ERROR << "创建文件夹失败: " << result.error().message
                      << " (user_id=" << user_id << ")";
            co_return Response::Error(result.error());
        }

        // 4. 构造响应
        LOG_INFO << "创建文件夹成功: " << result->name << " (ID: " << result->id
                 << ", user_id: " << user_id << ")";
        co_return Response::Success(result->ToJson());
    }

    auto FolderController::GetTree(drogon::HttpRequestPtr request)
        -> drogon::Task<drogon::HttpResponsePtr> {

        LOG_INFO << "收到获取文件夹树请求: " << request->getPeerAddr().toIpPort();

        // 1. 解析并验证请求参数
        auto parse_result = FolderTreeRequest::FromRequest(request);
        if (!parse_result) {
            LOG_WARN << "文件夹树请求参数验证失败: " << parse_result.error().message;
            co_return Response::Error(parse_result.error());
        }

        // 2. 从请求属性获取 user_id（由 JwtAuthFilter 设置）
        const auto user_id = request->attributes()->get<uint64_t>("user_id");

        // 3. 调用 Service 层获取文件夹树
        auto result = co_await m_folder_service->GetFolderTree(
            user_id,
            parse_result->parent_id,
            parse_result->depth
        );
        if (!result) {
            LOG_ERROR << "获取文件夹树失败: " << result.error().message
                      << " (user_id=" << user_id << ")";
            co_return Response::Error(result.error());
        }

        // 4. 返回成功响应
        LOG_INFO << "获取文件夹树成功: user_id=" << user_id
                 << ", parent_id=" << parse_result->parent_id
                 << ", depth=" << parse_result->depth;
        co_return Response::Success(result->ToJson());
    }

    auto FolderController::GetBreadcrumb(drogon::HttpRequestPtr request)
        -> drogon::Task<drogon::HttpResponsePtr> {

        LOG_INFO << "收到获取面包屑请求: " << request->getPeerAddr().toIpPort();

        // 1. 从路径参数解析 folder_id
        auto folder_id_str = request->getParameter("folder_id");
        uint64_t folder_id = 0;
        try {
            folder_id = std::stoull(folder_id_str);
        } catch (...) {
            LOG_WARN << "folder_id 格式无效: " << folder_id_str;
            co_return Response::Error(ErrorInfo(ErrorCode::InvalidParameter, "folder_id 格式无效"));
        }

        // 2. 从请求属性获取 user_id（由 JwtAuthFilter 设置）
        const auto user_id = request->attributes()->get<uint64_t>("user_id");

        // 3. 调用 Service 层获取面包屑
        auto result = co_await m_folder_service->GetBreadcrumb(folder_id, user_id);
        if (!result) {
            LOG_ERROR << "获取面包屑失败: " << result.error().message
                      << " (user_id=" << user_id << ", folder_id=" << folder_id << ")";
            co_return Response::Error(result.error());
        }

        // 4. 返回成功响应
        LOG_INFO << "获取面包屑成功: user_id=" << user_id
                 << ", folder_id=" << folder_id
                 << ", path_count=" << result->path.size();
        co_return Response::Success(result->ToJson());
    }

} // namespace disk::folder
