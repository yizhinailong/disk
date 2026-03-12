/**
 * @file Response.hpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 统一 API 响应构造器
 *
 * @copyright Copyright (c) 2026
 *
 */

#pragma once

#include <string>
#include <utility>

#include <drogon/HttpResponse.h>
#include <json/json.h>

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
        /**
         * @brief 创建分页信息
         * @param page 当前页码
         * @param page_size 每页数量
         * @param total 总记录数
         * @return Pagination 分页信息
         */
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
        static auto Success() -> drogon::HttpResponsePtr {
            return Success(Json::Value{ Json::nullValue });
        }

        /// 成功响应（带数据）
        /**
         * @brief 成功响应（带数据）
         * @param data 响应数据
         * @return drogon::HttpResponsePtr HTTP响应对象
         */
        [[nodiscard]]
        static auto Success(const Json::Value& data) -> drogon::HttpResponsePtr {
            Json::Value json;
            json["code"] = Error::ToInt(ErrorCode::Success);
            json["message"] = Error::GetErrorMessage(ErrorCode::Success);
            json["data"] = data;

            auto response = drogon::HttpResponse::newHttpJsonResponse(json);
            response->setStatusCode(Error::GetHttpStatus(ErrorCode::Success));
            return response;
        }

        /// 分页响应
        /**
         * @brief 分页响应
         * @param items 数据项
         * @param pagination 分页信息
         * @return drogon::HttpResponsePtr 分页HTTP响应对象
         */
        [[nodiscard]]
        static auto Paginated(const Json::Value& items, const Pagination& pagination) -> drogon::HttpResponsePtr {
            Json::Value data;
            data["items"] = items;
            data["pagination"] = pagination.ToJson();
            return Success(data);
        }

        // ==================== 错误响应 ====================
        /// 错误响应（从 Err 结构体）
        [[nodiscard]]
        static auto Error(const ::ErrorInfo& err) -> drogon::HttpResponsePtr {
            Json::Value json;
            json["code"] = err.CodeInt();
            json["message"] = err.message;
            json["data"] = Json::Value{ Json::nullValue };

            auto response = drogon::HttpResponse::newHttpJsonResponse(json);
            response->setStatusCode(err.HttpStatus());
            return response;
        }

        /// 错误响应（使用错误码默认消息）
        /**
         * @brief 错误响应
         * @param code 错误码
         * @return drogon::HttpResponsePtr 错误HTTP响应对象
         */
        [[nodiscard]]
        static auto Error(ErrorCode code) -> drogon::HttpResponsePtr {
            return Error(ErrorInfo(code));
        }

        /// 错误响应（错误码 + 自定义消息）
        /**
         * @brief 失败响应
         * @param code 错误码
         * @param message 错误消息
         * @return drogon::HttpResponsePtr 失败HTTP响应对象
         */
        [[nodiscard]]
        static auto Fail(ErrorCode code, const std::string& message) -> drogon::HttpResponsePtr {
            return Error(ErrorInfo(code, message));
        }

        // ==================== Result 类型支持 ====================

        /// 从 Result<T> 构造响应
        /**
         * @brief 从Result构造HTTP响应
         * @param result Result对象
         * @param func 转换函数
         * @return drogon::HttpResponsePtr 从Result构造的HTTP响应对象
         */
        template <typename T, typename Func>
        [[nodiscard]]
        static auto FromResult(const Result<T>& result, Func&& to_json) -> drogon::HttpResponsePtr {
            if (result.has_value()) {
                return Success(std::forward<Func>(to_json)(result.value()));
            }
            return Error(result.error());
        }

        /// 从 Result<void> 构造响应
        /**
         * @brief 从Result<void>构造HTTP响应
         * @param result Result<void>对象
         * @return drogon::HttpResponsePtr 从Result<void>构造的HTTP响应对象
         */
        [[nodiscard]]
        static auto FromResult(const Result<void>& result) -> drogon::HttpResponsePtr {
            if (result.has_value()) {
                return Success();
            }
            return Error(result.error());
        }
    };
} // namespace disk
