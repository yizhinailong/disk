/**
 * @file TrashDto.hpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 回收站模块数据传输对象（Data Transfer Objects）
 *
 * @copyright Copyright (c) 2026
 *
 * @details
 * 本文件包含回收站模块的所有数据传输对象（DTO）：
 * - TrashListRequest: 回收站列表请求（分页）
 * - TrashBatchRequest: 批量操作请求（恢复/删除）
 * - TrashItemResponse: 回收站项目响应
 * - BatchRestoreResponse: 批量恢复响应
 * - BatchDeleteResponse: 批量删除响应
 * - DeleteAllResponse: 清空回收站响应
 *
 * DTO 用于在不同层（Controller、Service）之间传输数据，
 * 包含请求验证和响应序列化逻辑。
 */

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <drogon/HttpRequest.h>
#include <json/json.h>

#include "utils/DtoBase.hpp"
#include "utils/ErrorCode.hpp"

namespace disk::trash {

    /// ==================== Request DTOs ====================

    /**
     * @brief 回收站列表请求 DTO
     *
     * @details
     * 验证规则：
     * - page: 可选，默认1，最小值1
     * - page_size: 可选，默认20，范围1-100
     */
    struct TrashListRequest : DtoBase<TrashListRequest> {
        int page{ 1 };
        int page_size{ 20 };

        /// 从 HTTP 请求解析并验证，返回 Result
        [[nodiscard]]
        static auto FromRequest(const drogon::HttpRequestPtr& req) -> Result<TrashListRequest> {
            Logger::Debug() << "Start parsing trash list request parameters";

            TrashListRequest request;

            auto page_result = QueryPositiveInt(req, "page", 1);
            if (!page_result) return std::unexpected(page_result.error());
            if (page_result->has_value()) {
                request.page = **page_result;
            }

            auto page_size_result = QueryPositiveInt(req, "page_size", 1, 100);
            if (!page_size_result) return std::unexpected(page_size_result.error());
            if (page_size_result->has_value()) {
                request.page_size = **page_size_result;
            }

            Logger::Debug() << "Parsed trash list request: page=" << request.page
                      << ", page_size=" << request.page_size;

            return request;
        }
    };

    /**
     * @brief 批量操作请求 DTO（恢复/删除）
     *
     * @details
     * 验证规则：
     * - trash_ids: 必填，非空数组，元素为正整数
     * - 数组长度限制：1-100
     */
    struct TrashBatchRequest : DtoBase<TrashBatchRequest> {
        std::vector<uint64_t> trash_ids;

        /// 从 HTTP 请求解析并验证，返回 Result
        [[nodiscard]]
        static auto FromRequest(const drogon::HttpRequestPtr& req) -> Result<TrashBatchRequest> {
            Logger::Debug() << "Start parsing batch operation request parameters";

            auto json_result = RequireJsonBody(req);
            if (!json_result) return std::unexpected(json_result.error());
            const auto& json = *json_result.value();

            auto ids_result = RequirePositiveIdArray(json, "trash_ids");
            if (!ids_result) return std::unexpected(ids_result.error());

            TrashBatchRequest request;
            request.trash_ids = std::move(*ids_result);

            Logger::Debug() << "Parsed batch operation request: " << request.trash_ids.size() << " IDs";

            return request;
        }
    };

    /// ==================== Response DTOs ====================

    /**
     * @brief 批量操作摘要
     *
     * @details
     * 包含批量操作的成功/失败统计。
     */
    struct BatchSummary : DtoBase<BatchSummary> {
        int total{ 0 };
        int success_count{ 0 };
        int failure_count{ 0 };

        /// 转换为 JSON
        [[nodiscard]]
        auto ToJson() const -> Json::Value {
            Json::Value json;
            SetField(json, "total", total);
            SetField(json, "success_count", success_count);
            SetField(json, "failure_count", failure_count);
            return json;
        }
    };

    /**
     * @brief 批量操作结果项（恢复/删除）
     *
     * @details
     * 每个回收站项目的操作结果，包含成功或失败的详细信息。
     * JSON 输出格式与 API 文档合同一致：
     * - 成功恢复：{trash_id, status, file_id/folder_id, path}
     * - 成功删除：{trash_id, status, freed_space}
     * - 失败：{trash_id, status, error: {code, message, field?, value?}}
     */
    struct BatchResultItem : DtoBase<BatchResultItem> {
        uint64_t trash_id{ 0 };
        std::string status; ///< "success" or "failed"

        /// 失败时的错误信息
        std::optional<uint16_t> code;
        std::optional<std::string> message;
        std::optional<std::string> field; ///< 错误字段名（可选）
        std::optional<std::string> value; ///< 错误字段值（可选）

        /// 成功恢复时的信息
        std::optional<uint64_t> file_id;
        std::optional<uint64_t> folder_id;
        std::optional<std::string> path;

        /// 成功删除时的信息
        std::optional<uint64_t> freed_space;

        /// 转换为 JSON
        [[nodiscard]]
        auto ToJson() const -> Json::Value {
            Json::Value json;
            SetField(json, "trash_id", trash_id);
            SetField(json, "status", status);

            if (status == "failed") {
                Json::Value error_obj;
                if (code.has_value()) {
                    error_obj["code"] = code.value();
                }
                if (message.has_value()) {
                    error_obj["message"] = message.value();
                }
                if (field.has_value()) {
                    error_obj["field"] = field.value();
                }
                if (value.has_value()) {
                    error_obj["value"] = value.value();
                }
                json["error"] = error_obj;
            } else {
                SetOptional(json, "file_id", file_id);
                SetOptional(json, "folder_id", folder_id);
                SetOptional(json, "path", path);
                SetOptional(json, "freed_space", freed_space);
            }

            return json;
        }
    };

    /**
     * @brief 回收站项目响应 DTO
     *
     * @details
     * 包含回收站项目的完整信息。
     * 字段命名与 API 文档合同一致。
     */
    struct TrashItemResponse : DtoBase<TrashItemResponse> {
        uint64_t id;
        std::string type; ///< "file" or "folder"
        uint64_t original_id;
        std::string name;
        uint64_t size;
        std::string original_path;
        std::string deleted_at;
        std::string expires_at;

        /// 转换为 JSON
        [[nodiscard]]
        auto ToJson() const -> Json::Value {
            Json::Value json;
            SetField(json, "id", id);
            SetField(json, "type", type);
            SetField(json, "original_id", original_id);
            SetField(json, "name", name);
            SetField(json, "size", size);
            SetField(json, "original_path", original_path);
            SetField(json, "deleted_at", deleted_at);
            SetField(json, "expires_at", expires_at);
            return json;
        }
    };

    /**
     * @brief 批量恢复响应 DTO
     *
     * @details
     * 包含批量恢复操作的摘要和每个项目的结果。
     */
    struct BatchRestoreResponse : DtoBase<BatchRestoreResponse> {
        BatchSummary summary;
        std::vector<BatchResultItem> results;

        /// 转换为 JSON
        [[nodiscard]]
        auto ToJson() const -> Json::Value {
            Json::Value json;
            SetField(json, "summary", summary);
            SetArray(json, "results", results);
            return json;
        }
    };

    /**
     * @brief 批量删除响应 DTO
     *
     * @details
     * 包含批量删除操作的摘要和每个项目的结果。
     */
    struct BatchDeleteResponse : DtoBase<BatchDeleteResponse> {
        BatchSummary summary;
        std::vector<BatchResultItem> results;

        /// 转换为 JSON
        [[nodiscard]]
        auto ToJson() const -> Json::Value {
            Json::Value json;
            SetField(json, "summary", summary);
            SetArray(json, "results", results);
            return json;
        }
    };

    /**
     * @brief 清空回收站响应 DTO
     *
     * @details
     * 包含清空回收站的统计信息。
     */
    struct DeleteAllResponse : DtoBase<DeleteAllResponse> {
        int deleted_count{ 0 };
        uint64_t freed_space{ 0 };

        /// 转换为 JSON
        [[nodiscard]]
        auto ToJson() const -> Json::Value {
            Json::Value json;
            SetField(json, "deleted_count", deleted_count);
            SetField(json, "freed_space", freed_space);
            return json;
        }
    };

} ///< namespace disk::trash
