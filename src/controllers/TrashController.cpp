/**
 * @file TrashController.cpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 回收站控制器实现
 *
 * @copyright Copyright (c) 2026
 *
 */

#include "TrashController.hpp"

#include "dtos/TrashDto.hpp"
#include "utils/Response.hpp"

namespace disk::trash {

    TrashController::TrashController()
        : m_trash_service(std::make_unique<TrashService>(drogon::app().getDbClient())) {
    }

    auto TrashController::List(drogon::HttpRequestPtr request)
        -> drogon::Task<drogon::HttpResponsePtr> {

        LOG_INFO << "Received get trash list request: " << request->getPeerAddr().toIpPort();

        // 步骤 1: 从请求属性中提取 user_id（由 JwtAuthFilter 设置）
        const auto user_id = request->attributes()->get<uint64_t>("user_id");

        // 步骤 2: 解析并验证请求 DTO
        auto parse_result = TrashListRequest::FromRequest(request);
        if (!parse_result) {
            LOG_WARN << "Trash list request parameter validation failed: "
                     << parse_result.error().message;
            co_return Response::Error(parse_result.error());
        }

        // 步骤 3: 调用服务层获取回收站项目
        auto list_result =
            co_await m_trash_service->List(user_id, parse_result->page, parse_result->page_size);
        if (!list_result) {
            LOG_ERROR << "Failed to get trash list: " << list_result.error().message;
            co_return Response::Error(list_result.error());
        }

        // 步骤 4: 获取总数用于分页
        auto count_result = co_await m_trash_service->Count(user_id);
        if (!count_result) {
            LOG_ERROR << "Failed to get trash count: " << count_result.error().message;
            co_return Response::Error(count_result.error());
        }

        // 步骤 5: 构造分页响应
        Json::Value items(Json::arrayValue);
        for (const auto& item : *list_result) {
            items.append(item.ToJson());
        }

        auto pagination =
            Pagination::Create(parse_result->page, parse_result->page_size, *count_result);

        LOG_INFO << "Get trash list successful: user_id=" << user_id << ", total=" << *count_result;
        co_return Response::Paginated(items, pagination);
    }

    auto TrashController::Restore(drogon::HttpRequestPtr request)
        -> drogon::Task<drogon::HttpResponsePtr> {

        LOG_INFO << "Received batch restore request: " << request->getPeerAddr().toIpPort();

        // 步骤 1: 从请求属性中提取 user_id（由 JwtAuthFilter 设置）
        const auto user_id = request->attributes()->get<uint64_t>("user_id");

        // 步骤 2: 解析并验证请求 DTO
        auto parse_result = TrashBatchRequest::FromRequest(request);
        if (!parse_result) {
            LOG_WARN << "Batch restore request parameter validation failed: "
                     << parse_result.error().message;
            co_return Response::Error(parse_result.error());
        }

        // 步骤 3: 调用服务层恢复项目
        auto restore_result = co_await m_trash_service->Restore(user_id, parse_result->trash_ids);
        if (!restore_result) {
            LOG_ERROR << "Batch restore failed: " << restore_result.error().message;
            co_return Response::Error(restore_result.error());
        }

        // 步骤 4: 返回成功响应（包含批量操作结果）
        LOG_INFO << "Batch restore completed: user_id=" << user_id
                 << ", total=" << restore_result->summary.total
                 << ", success=" << restore_result->summary.success_count
                 << ", failure=" << restore_result->summary.failure_count;
        co_return Response::Success(restore_result->ToJson());
    }

    auto TrashController::Delete(drogon::HttpRequestPtr request)
        -> drogon::Task<drogon::HttpResponsePtr> {

        LOG_INFO << "Received batch permanent delete request: "
                 << request->getPeerAddr().toIpPort();

        // 步骤 1: 从请求属性中提取 user_id（由 JwtAuthFilter 设置）
        const auto user_id = request->attributes()->get<uint64_t>("user_id");

        // 步骤 2: 解析并验证请求 DTO
        auto parse_result = TrashBatchRequest::FromRequest(request);
        if (!parse_result) {
            LOG_WARN << "Batch delete request parameter validation failed: "
                     << parse_result.error().message;
            co_return Response::Error(parse_result.error());
        }

        // 步骤 3: 调用服务层删除项目
        auto delete_result = co_await m_trash_service->Delete(user_id, parse_result->trash_ids);
        if (!delete_result) {
            LOG_ERROR << "Batch delete failed: " << delete_result.error().message;
            co_return Response::Error(delete_result.error());
        }

        // 步骤 4: 返回成功响应（包含批量操作结果）
        LOG_INFO << "Batch delete completed: user_id=" << user_id
                 << ", total=" << delete_result->summary.total
                 << ", success=" << delete_result->summary.success_count
                 << ", failure=" << delete_result->summary.failure_count;
        co_return Response::Success(delete_result->ToJson());
    }

    auto TrashController::DeleteAll(drogon::HttpRequestPtr request)
        -> drogon::Task<drogon::HttpResponsePtr> {

        LOG_INFO << "Received empty trash request: " << request->getPeerAddr().toIpPort();

        // 步骤 1: 从请求属性中提取 user_id（由 JwtAuthFilter 设置）
        const auto user_id = request->attributes()->get<uint64_t>("user_id");

        // 步骤 2: 调用服务层清空回收站
        auto delete_all_result = co_await m_trash_service->DeleteAll(user_id);
        if (!delete_all_result) {
            LOG_ERROR << "Failed to empty trash: " << delete_all_result.error().message;
            co_return Response::Error(delete_all_result.error());
        }

        // 步骤 3: 返回成功响应（包含删除统计）
        LOG_INFO << "Empty trash completed: user_id=" << user_id
                 << ", deleted_count=" << delete_all_result->deleted_count
                 << ", freed_space=" << delete_all_result->freed_space;
        co_return Response::Success(delete_all_result->ToJson());
    }

} // namespace disk::trash
