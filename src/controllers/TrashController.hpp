/**
 * @file TrashController.hpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 回收站控制器
 * @note Request 和 Response DTO 定义在 dtos/TrashDto.hpp
 *
 * @copyright Copyright (c) 2026
 *
 */

#pragma once

#include "services/TrashService.hpp"

namespace disk::trash {

    // ==================== Controller ====================

    /**
     * @brief 回收站控制器
     *
     * 业务规则：
     * - 所有接口需要 JWT 认证（通过 JwtAuthFilter 自动处理）
     * - 用户信息从请求 attributes 中的 user_id 获取（由 JwtAuthFilter 设置）
     * - List: 获取回收站列表（分页）
     * - Restore: 批量恢复回收站项目
     * - Delete: 批量彻底删除回收站项目
     * - DeleteAll: 清空回收站
     */
    class TrashController : public drogon::HttpController<TrashController> {
    public:
        TrashController();

        METHOD_LIST_BEGIN
        ADD_METHOD_TO(
            TrashController::List,
            "/api/trash",
            drogon::Get,
            "disk::filters::JwtAuthFilter",
            "disk::filters::RateLimitFilter"
        );
        ADD_METHOD_TO(
            TrashController::Restore,
            "/api/trash/restore",
            drogon::Post,
            "disk::filters::JwtAuthFilter",
            "disk::filters::RateLimitFilter"
        );
        ADD_METHOD_TO(
            TrashController::Delete,
            "/api/trash",
            drogon::Delete,
            "disk::filters::JwtAuthFilter",
            "disk::filters::RateLimitFilter"
        );
        ADD_METHOD_TO(
            TrashController::DeleteAll,
            "/api/trash/all",
            drogon::Delete,
            "disk::filters::JwtAuthFilter",
            "disk::filters::RateLimitFilter"
        );
        METHOD_LIST_END

        /**
         * @brief 获取回收站列表
         *
         * 业务规则：
         * - 从请求 attributes 中提取 user_id（由 JwtAuthFilter 设置）
         * - 解析查询参数 page 和 page_size（可选，默认 1/20）
         * - 调用 TrashService::List(user_id, page, page_size) 获取列表
         * - 调用 TrashService::Count(user_id) 获取总数
         * - 返回分页响应（Response::Paginated）
         *
         * @param request HTTP请求对象
         * @return drogon::Task<drogon::HttpResponsePtr> HTTP响应
         */
        [[nodiscard]]
        auto List(drogon::HttpRequestPtr request) -> drogon::Task<drogon::HttpResponsePtr>;

        /**
         * @brief 批量恢复回收站项目
         *
         * 业务规则：
         * - 从请求 attributes 中提取 user_id（由 JwtAuthFilter 设置）
         * - 解析 JSON 请求体提取 trash_ids 数组
         * - 调用 TrashService::Restore(user_id, trash_ids) 执行恢复
         * - 返回批量操作结果（包含每项操作的成功/失败详情）
         *
         * @param request HTTP请求对象
         * @return drogon::Task<drogon::HttpResponsePtr> HTTP响应
         */
        [[nodiscard]]
        auto Restore(drogon::HttpRequestPtr request) -> drogon::Task<drogon::HttpResponsePtr>;

        /**
         * @brief 批量彻底删除回收站项目
         *
         * 业务规则：
         * - 从请求 attributes 中提取 user_id（由 JwtAuthFilter 设置）
         * - 解析 JSON 请求体提取 trash_ids 数组
         * - 调用 TrashService::Delete(user_id, trash_ids) 执行删除
         * - 返回批量操作结果（包含每项操作的成功/失败详情）
         *
         * @param request HTTP请求对象
         * @return drogon::Task<drogon::HttpResponsePtr> HTTP响应
         */
        [[nodiscard]]
        auto Delete(drogon::HttpRequestPtr request) -> drogon::Task<drogon::HttpResponsePtr>;

        /**
         * @brief 清空回收站
         *
         * 业务规则：
         * - 从请求 attributes 中提取 user_id（由 JwtAuthFilter 设置）
         * - 调用 TrashService::DeleteAll(user_id) 清空回收站
         * - 返回删除统计（deleted_count 和 freed_space）
         *
         * @param request HTTP请求对象
         * @return drogon::Task<drogon::HttpResponsePtr> HTTP响应
         */
        [[nodiscard]]
        auto DeleteAll(drogon::HttpRequestPtr request) -> drogon::Task<drogon::HttpResponsePtr>;

    private:
        std::unique_ptr<TrashService> m_trash_service;
    };

} // namespace disk::trash
