/**
 * @file TrashController.cpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 回收站控制器实现
 *
 * @copyright Copyright (c) 2026
 *
 */

#include "TrashController.hpp"

#include "controllers/ControllerHelpers.hpp"
#include "dtos/TrashDto.hpp"
#include "services/ObservedDbClient.hpp"
#include "utils/Response.hpp"

namespace disk::trash {

    TrashController::TrashController()
        : m_trash_service(std::make_unique<TrashService>(disk::metrics::ObserveDbClient(drogon::app().getDbClient()))) {
    }

    auto TrashController::List(drogon::HttpRequestPtr request)
        -> drogon::Task<drogon::HttpResponsePtr> {
        auto log_context = disk::controllers::GetRequestLogContext(request, "trash");

        Logger::Info(log_context)
            << "Received get trash list request: " << request->getPeerAddr().toIpPort();

        /// 步骤 1: 从请求属性中提取 user_id（由 JwtAuthFilter 设置）
        const auto user_id = disk::controllers::GetAuthenticatedUserId(request);

        /// 步骤 2: 解析并验证请求 DTO
        auto parse_result = TrashListRequest::FromRequest(request, log_context);
        if (!parse_result) {
            Logger::Warn(log_context)
                << "Trash list request parameter validation failed: "
                << parse_result.error().message;
            co_return Response::Error(parse_result.error());
        }

        /// 步骤 3: 调用服务层获取回收站项目
        auto list_result = co_await m_trash_service->List(
            user_id,
            parse_result->page,
            parse_result->page_size,
            log_context
        );
        if (!list_result) {
            Logger::Error(log_context)
                << "Failed to get trash list: " << list_result.error().message;
            co_return Response::Error(list_result.error());
        }

        /// 步骤 4: 获取总数用于分页
        auto count_result = co_await m_trash_service->Count(user_id, log_context);
        if (!count_result) {
            Logger::Error(log_context)
                << "Failed to get trash count: " << count_result.error().message;
            co_return Response::Error(count_result.error());
        }

        /// 步骤 5: 构造分页响应
        Json::Value items(Json::arrayValue);
        for (const auto& item : *list_result) {
            items.append(item.ToJson());
        }

        auto pagination =
            Pagination::Create(parse_result->page, parse_result->page_size, *count_result);

        Logger::Info(log_context)
            << "Get trash list successful: user_id=" << user_id << ", total=" << *count_result;
        co_return Response::Paginated(items, pagination);
    }

    auto TrashController::Restore(drogon::HttpRequestPtr request)
        -> drogon::Task<drogon::HttpResponsePtr> {
        auto log_context = disk::controllers::GetRequestLogContext(request, "trash");

        Logger::Info(log_context)
            << "Received batch restore request: " << request->getPeerAddr().toIpPort();

        /// 步骤 1: 从请求属性中提取 user_id（由 JwtAuthFilter 设置）
        const auto user_id = disk::controllers::GetAuthenticatedUserId(request);

        /// 步骤 2: 解析并验证请求 DTO
        auto parse_result = TrashBatchRequest::FromRequest(request, log_context);
        if (!parse_result) {
            Logger::Warn(log_context)
                << "Batch restore request parameter validation failed: "
                << parse_result.error().message;
            co_return Response::Error(parse_result.error());
        }

        /// 步骤 3: 调用服务层恢复项目
        auto restore_result = co_await m_trash_service->Restore(
            user_id,
            parse_result->trash_ids,
            log_context
        );
        if (!restore_result) {
            Logger::Error(log_context)
                << "Batch restore failed: " << restore_result.error().message;
            co_return Response::Error(restore_result.error());
        }

        /// 步骤 4: 返回成功响应（包含批量操作结果）
        Logger::Info(log_context)
            << "Batch restore completed: user_id=" << user_id
            << ", total=" << restore_result->summary.total
            << ", success=" << restore_result->summary.success_count
            << ", failure=" << restore_result->summary.failure_count;
        co_return Response::Success(restore_result->ToJson());
    }

    auto TrashController::Delete(drogon::HttpRequestPtr request)
        -> drogon::Task<drogon::HttpResponsePtr> {
        auto log_context = disk::controllers::GetRequestLogContext(request, "trash");

        Logger::Info(log_context)
            << "Received batch permanent delete request: "
            << request->getPeerAddr().toIpPort();

        /// 步骤 1: 从请求属性中提取 user_id（由 JwtAuthFilter 设置）
        const auto user_id = disk::controllers::GetAuthenticatedUserId(request);

        /// 步骤 2: 解析并验证请求 DTO
        auto parse_result = TrashBatchRequest::FromRequest(request, log_context);
        if (!parse_result) {
            Logger::Warn(log_context)
                << "Batch delete request parameter validation failed: "
                << parse_result.error().message;
            co_return Response::Error(parse_result.error());
        }

        /// 步骤 3: 调用服务层删除项目
        auto delete_result = co_await m_trash_service->Delete(
            user_id,
            parse_result->trash_ids,
            log_context
        );
        if (!delete_result) {
            Logger::Error(log_context)
                << "Batch delete failed: " << delete_result.error().message;
            co_return Response::Error(delete_result.error());
        }

        /// 步骤 4: 返回成功响应（包含批量操作结果）
        Logger::Info(log_context)
            << "Batch delete completed: user_id=" << user_id
            << ", total=" << delete_result->summary.total
            << ", success=" << delete_result->summary.success_count
            << ", failure=" << delete_result->summary.failure_count;
        co_return Response::Success(delete_result->ToJson());
    }

    auto TrashController::DeleteAll(drogon::HttpRequestPtr request)
        -> drogon::Task<drogon::HttpResponsePtr> {
        auto log_context = disk::controllers::GetRequestLogContext(request, "trash");

        Logger::Info(log_context)
            << "Received empty trash request: " << request->getPeerAddr().toIpPort();

        /// 步骤 1: 从请求属性中提取 user_id（由 JwtAuthFilter 设置）
        const auto user_id = disk::controllers::GetAuthenticatedUserId(request);

        /// 步骤 2: 调用服务层清空回收站
        auto delete_all_result = co_await m_trash_service->DeleteAll(user_id, log_context);
        if (!delete_all_result) {
            Logger::Error(log_context)
                << "Failed to empty trash: " << delete_all_result.error().message;
            co_return Response::Error(delete_all_result.error());
        }

        /// 步骤 3: 返回成功响应（包含删除统计）
        Logger::Info(log_context)
            << "Empty trash completed: user_id=" << user_id
            << ", deleted_count=" << delete_all_result->deleted_count
            << ", freed_space=" << delete_all_result->freed_space;
        co_return Response::Success(delete_all_result->ToJson());
    }

} // namespace disk::trash
