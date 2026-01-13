/**
 * @file Response.hpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 统一 API 响应构造器
 * @version 0.1
 * @date 2026-01-14
 *
 * @copyright Copyright (c) 2026
 *
 */

#pragma once

#include "ErrorCode.hpp"

namespace disk {

    /**
     * @brief 分页信息结构体
     */
    struct Pagination {
        int page{ 1 };
        int page_size{ 20 };
        int total{ 0 };
        int total_pages{ 0 };

        [[nodiscard]]
        auto ToJson() const -> Json::Value {
            Json::Value json;
            json["page"] = page;
            json["page_size"] = page_size;
            json["total"] = total;
            json["total_pages"] = total_pages;
            return json;
        }

        /// 便捷构造：自动计算 total_pages
        static auto Create(int page, int page_size, int total) -> Pagination {
            return {
                .page = page,
                .page_size = page_size,
                .total = total,
                .total_pages = page_size > 0 ? (total + page_size - 1) / page_size : 0
            };
        }
    };

    /**
     * @brief 统一 API 响应构造器
     *
     * 响应格式：
     * - 成功: {"code": 0, "message": "success", "data": {...}}
     * - 错误: {"code": 40001, "message": "错误信息", "data": null}
     * - 分页: {"code": 0, "message": "success", "data": {"items": [], "pagination": {...}}}
     */
    class Response {
    public:
        // ==================== 成功响应 ====================
        /// 成功响应（无数据）
        [[nodiscard]]
        static auto Success() -> HttpResponsePtr {
            return Success(Json::Value{ Json::nullValue });
        }

        /// 成功响应（带数据）
        [[nodiscard]]
        static auto Success(const Json::Value& data) -> HttpResponsePtr {
            Json::Value json;
            json["code"] = Error::ToInt(ErrorCode::Success);
            json["message"] = Error::GetErrorMessage(ErrorCode::Success);
            json["data"] = data;

            auto response = HttpResponse::newHttpJsonResponse(json);
            response->setStatusCode(Error::GetHttpStatus(ErrorCode::Success));
            return response;
        }

        /// 分页响应
        [[nodiscard]]
        static auto Paginated(const Json::Value& items, const Pagination& pagination) -> HttpResponsePtr {
            Json::Value data;
            data["items"] = items;
            data["pagination"] = pagination.ToJson();
            return Success(data);
        }

        // ==================== 错误响应 ====================
        /// 错误响应（使用错误码默认消息）
        [[nodiscard]]
        static auto Error(ErrorCode code) -> HttpResponsePtr {
            return Error(code, Error::GetErrorMessage(code));
        }

        /// 错误响应（自定义消息）
        [[nodiscard]]
        static auto Error(ErrorCode code, const std::string& message) -> HttpResponsePtr {
            Json::Value json;
            json["code"] = Error::ToInt(code);
            json["message"] = message;
            json["data"] = Json::Value{ Json::nullValue };

            auto response = HttpResponse::newHttpJsonResponse(json);
            response->setStatusCode(Error::GetHttpStatus(code));
            return response;
        }

        // ==================== Result 类型支持 ====================

        /// 从 Result<T> 构造响应
        template <typename T, typename Func>
        [[nodiscard]]
        static auto FromResult(const Result<T>& result, Func&& to_json) -> HttpResponsePtr {
            if (result.has_value()) {
                return Success(std::forward<Func>(to_json)(result.value()));
            }
            return Error(result.error());
        }

        /// 从 VoidResult 构造响应
        [[nodiscard]]
        static auto fromResult(const VoidResult& result) -> HttpResponsePtr {
            if (result.has_value()) {
                return Success();
            }
            return Error(result.error());
        }
    };
} // namespace disk
