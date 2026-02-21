/**
 * @file TrashController.cpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 回收站控制器实现
 * @version 0.1
 * @date 2026-02-15
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
        LOG_DEBUG << "TrashController initialized";
    }

    auto TrashController::List(drogon::HttpRequestPtr request)
        -> drogon::Task<drogon::HttpResponsePtr> {

        LOG_INFO << "Received get trash list request: " << request->getPeerAddr().toIpPort();

        // Step 1: Extract user_id from request attributes (set by JwtAuthFilter)
        const auto user_id = request->attributes()->get<uint64_t>("user_id");

        // Step 2: Parse and validate request DTO
        auto parse_result = TrashListRequest::FromRequest(request);
        if (!parse_result) {
            LOG_WARN << "Trash list request parameter validation failed: "
                     << parse_result.error().message;
            co_return Response::Error(parse_result.error());
        }

        // Step 3: Call service to get trash items
        auto list_result =
            co_await m_trash_service->List(user_id, parse_result->page, parse_result->page_size);
        if (!list_result) {
            LOG_ERROR << "Failed to get trash list: " << list_result.error().message;
            co_return Response::Error(list_result.error());
        }

        // Step 4: Get total count for pagination
        auto count_result = co_await m_trash_service->Count(user_id);
        if (!count_result) {
            LOG_ERROR << "Failed to get trash count: " << count_result.error().message;
            co_return Response::Error(count_result.error());
        }

        // Step 5: Build response with pagination
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

        // Step 1: Extract user_id from request attributes (set by JwtAuthFilter)
        const auto user_id = request->attributes()->get<uint64_t>("user_id");

        // Step 2: Parse and validate request DTO
        auto parse_result = TrashBatchRequest::FromRequest(request);
        if (!parse_result) {
            LOG_WARN << "Batch restore request parameter validation failed: "
                     << parse_result.error().message;
            co_return Response::Error(parse_result.error());
        }

        // Step 3: Call service to restore items
        auto restore_result = co_await m_trash_service->Restore(user_id, parse_result->trash_ids);
        if (!restore_result) {
            LOG_ERROR << "批量恢复失败: " << restore_result.error().message;
            co_return Response::Error(restore_result.error());
        }

        // Step 4: Return success response with batch results
        LOG_INFO << "批量恢复完成: user_id=" << user_id
                 << ", total=" << restore_result->summary.total
                 << ", success=" << restore_result->summary.success_count
                 << ", failure=" << restore_result->summary.failure_count;
        co_return Response::Success(restore_result->ToJson());
    }

    auto TrashController::Delete(drogon::HttpRequestPtr request)
        -> drogon::Task<drogon::HttpResponsePtr> {

        LOG_INFO << "收到批量彻底删除请求: " << request->getPeerAddr().toIpPort();

        // Step 1: Extract user_id from request attributes (set by JwtAuthFilter)
        const auto user_id = request->attributes()->get<uint64_t>("user_id");

        // Step 2: Parse and validate request DTO
        auto parse_result = TrashBatchRequest::FromRequest(request);
        if (!parse_result) {
            LOG_WARN << "批量删除请求参数验证失败: " << parse_result.error().message;
            co_return Response::Error(parse_result.error());
        }

        // Step 3: Call service to delete items
        auto delete_result = co_await m_trash_service->Delete(user_id, parse_result->trash_ids);
        if (!delete_result) {
            LOG_ERROR << "批量删除失败: " << delete_result.error().message;
            co_return Response::Error(delete_result.error());
        }

        // Step 4: Return success response with batch results
        LOG_INFO << "批量删除完成: user_id=" << user_id
                 << ", total=" << delete_result->summary.total
                 << ", success=" << delete_result->summary.success_count
                 << ", failure=" << delete_result->summary.failure_count;
        co_return Response::Success(delete_result->ToJson());
    }

    auto TrashController::DeleteAll(drogon::HttpRequestPtr request)
        -> drogon::Task<drogon::HttpResponsePtr> {

        LOG_INFO << "收到清空回收站请求: " << request->getPeerAddr().toIpPort();

        // Step 1: Extract user_id from request attributes (set by JwtAuthFilter)
        const auto user_id = request->attributes()->get<uint64_t>("user_id");

        // Step 2: Call service to clear all trash
        auto delete_all_result = co_await m_trash_service->DeleteAll(user_id);
        if (!delete_all_result) {
            LOG_ERROR << "清空回收站失败: " << delete_all_result.error().message;
            co_return Response::Error(delete_all_result.error());
        }

        // Step 3: Return success response with statistics
        LOG_INFO << "清空回收站完成: user_id=" << user_id
                 << ", deleted_count=" << delete_all_result->deleted_count
                 << ", freed_space=" << delete_all_result->freed_space;
        co_return Response::Success(delete_all_result->ToJson());
    }

} // namespace disk::trash
