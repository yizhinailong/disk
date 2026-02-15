/**
 * @file TrashDto.hpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 回收站模块数据传输对象（Data Transfer Objects）
 * @version 0.1
 * @date 2026-02-15
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

#include "utils/ErrorCode.hpp"

namespace disk::trash {

    // ==================== Request DTOs ====================

    /**
     * @brief 回收站列表请求 DTO
     *
     * @details
     * 验证规则：
     * - page: 可选，默认1，最小值1
     * - page_size: 可选，默认20，范围1-100
     */
    struct TrashListRequest {
        int page{ 1 };
        int page_size{ 20 };

        /// 从 HTTP 请求解析并验证，返回 Result
        [[nodiscard]]
        static auto FromRequest(const drogon::HttpRequestPtr& req) -> Result<TrashListRequest> {
            LOG_DEBUG << "开始解析回收站列表请求参数";

            TrashListRequest request;

            // 解析 page（可选，默认1）
            auto page_str = req->getParameter("page");
            if (!page_str.empty()) {
                try {
                    int page_value = std::stoi(page_str);
                    if (page_value < 1) {
                        LOG_WARN << "参数 'page' 必须大于等于1";
                        return std::unexpected(ErrorInfo(ErrorCode::ValidationFailed, "参数 'page' 必须大于等于1"));
                    }
                    request.page = page_value;
                } catch (const std::exception& e) {
                    LOG_WARN << "参数 'page' 格式错误: " << page_str;
                    return std::unexpected(ErrorInfo(ErrorCode::ValidationFailed, "参数 'page' 格式错误: 期望正整数"));
                }
            }

            // 解析 page_size（可选，默认20）
            auto page_size_str = req->getParameter("page_size");
            if (!page_size_str.empty()) {
                try {
                    int page_size_value = std::stoi(page_size_str);
                    if (page_size_value < 1 || page_size_value > 100) {
                        LOG_WARN << "参数 'page_size' 必须在1-100之间";
                        return std::unexpected(ErrorInfo(ErrorCode::ValidationFailed, "参数 'page_size' 必须在1-100之间"));
                    }
                    request.page_size = page_size_value;
                } catch (const std::exception& e) {
                    LOG_WARN << "参数 'page_size' 格式错误: " << page_size_str;
                    return std::unexpected(ErrorInfo(ErrorCode::ValidationFailed, "参数 'page_size' 格式错误: 期望1-100之间的整数"));
                }
            }

            LOG_DEBUG << "解析到回收站列表请求: page=" << request.page << ", page_size=" << request.page_size;

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
    struct TrashBatchRequest {
        std::vector<uint64_t> trash_ids;

        /// 从 HTTP 请求解析并验证，返回 Result
        [[nodiscard]]
        static auto FromRequest(const drogon::HttpRequestPtr& req) -> Result<TrashBatchRequest> {
            LOG_DEBUG << "开始解析批量操作请求参数";

            auto json_ptr = req->getJsonObject();
            if (!json_ptr) {
                LOG_WARN << "请求体不是有效的 JSON";
                return std::unexpected(ErrorInfo(ErrorCode::ValidationFailed, "请求体不是有效的 JSON"));
            }

            const auto& json = *json_ptr;

            // 检查必填字段
            if (!json.isMember("trash_ids")) {
                LOG_WARN << "缺少必需参数: trash_ids";
                return std::unexpected(ErrorInfo(ErrorCode::ValidationFailed, "缺少必需参数: trash_ids"));
            }

            // 检查字段类型
            if (!json["trash_ids"].isArray()) {
                LOG_WARN << "参数 'trash_ids' 类型错误: 期望数组";
                return std::unexpected(ErrorInfo(ErrorCode::ValidationFailed, "参数 'trash_ids' 类型错误: 期望数组"));
            }

            const auto& ids_array = json["trash_ids"];
            if (ids_array.empty()) {
                LOG_WARN << "参数 'trash_ids' 不能为空数组";
                return std::unexpected(ErrorInfo(ErrorCode::ValidationFailed, "参数 'trash_ids' 不能为空数组"));
            }

            if (ids_array.size() > 100) {
                LOG_WARN << "参数 'trash_ids' 超出最大长度限制 (100)";
                return std::unexpected(ErrorInfo(ErrorCode::ValidationFailed, "参数 'trash_ids' 最多支持100个ID"));
            }

            TrashBatchRequest request;
            request.trash_ids.reserve(ids_array.size());

            for (Json::ArrayIndex i = 0; i < ids_array.size(); ++i) {
                if (!ids_array[i].isIntegral()) {
                    LOG_WARN << "参数 'trash_ids[" << i << "]' 类型错误: 期望正整数";
                    return std::unexpected(ErrorInfo(
                        ErrorCode::ValidationFailed,
                        std::string("参数 'trash_ids[") + std::to_string(i) + "]' 类型错误: 期望正整数"
                    ));
                }

                auto id_value = ids_array[i].asUInt64();
                if (id_value == 0) {
                    LOG_WARN << "参数 'trash_ids[" << i << "]' 必须为正整数";
                    return std::unexpected(ErrorInfo(
                        ErrorCode::ValidationFailed,
                        std::string("参数 'trash_ids[") + std::to_string(i) + "]' 必须为正整数"
                    ));
                }
                request.trash_ids.push_back(id_value);
            }

            LOG_DEBUG << "解析到批量操作请求: " << request.trash_ids.size() << " 个ID";

            return request;
        }
    };

    // ==================== Response DTOs ====================

    /**
     * @brief 批量操作摘要
     *
     * @details
     * 包含批量操作的成功/失败统计。
     */
    struct BatchSummary {
        int total{ 0 };
        int success_count{ 0 };
        int failure_count{ 0 };

        /// 转换为 JSON
        [[nodiscard]]
        auto ToJson() const -> Json::Value {
            Json::Value json;
            json["total"] = total;
            json["success_count"] = success_count;
            json["failure_count"] = failure_count;
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
    struct BatchResultItem {
        uint64_t trash_id{ 0 };
        std::string status; // "success" or "failed"

        // 失败时的错误信息
        std::optional<uint16_t> code;
        std::optional<std::string> message;
        std::optional<std::string> field; // 错误字段名（可选）
        std::optional<std::string> value; // 错误字段值（可选）

        // 成功恢复时的信息
        std::optional<uint64_t> file_id;
        std::optional<uint64_t> folder_id;
        std::optional<std::string> path;

        // 成功删除时的信息
        std::optional<uint64_t> freed_space;

        /// 转换为 JSON
        [[nodiscard]]
        auto ToJson() const -> Json::Value {
            Json::Value json;
            json["trash_id"] = static_cast<Json::UInt64>(trash_id);
            json["status"] = status;

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
                // 成功时：扁平化字段，不嵌套 data 对象
                if (file_id.has_value()) {
                    json["file_id"] = static_cast<Json::UInt64>(file_id.value());
                }
                if (folder_id.has_value()) {
                    json["folder_id"] = static_cast<Json::UInt64>(folder_id.value());
                }
                if (path.has_value()) {
                    json["path"] = path.value();
                }
                if (freed_space.has_value()) {
                    json["freed_space"] = static_cast<Json::UInt64>(freed_space.value());
                }
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
    struct TrashItemResponse {
        uint64_t id;
        std::string type; // "file" or "folder"
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
            json["id"] = static_cast<Json::UInt64>(id);
            json["type"] = type;
            json["original_id"] = static_cast<Json::UInt64>(original_id);
            json["name"] = name;
            json["size"] = static_cast<Json::UInt64>(size);
            json["original_path"] = original_path;
            json["deleted_at"] = deleted_at;
            json["expires_at"] = expires_at;
            return json;
        }
    };

    /**
     * @brief 批量恢复响应 DTO
     *
     * @details
     * 包含批量恢复操作的摘要和每个项目的结果。
     */
    struct BatchRestoreResponse {
        BatchSummary summary;
        std::vector<BatchResultItem> results;

        /// 转换为 JSON
        [[nodiscard]]
        auto ToJson() const -> Json::Value {
            Json::Value json;
            json["summary"] = summary.ToJson();

            Json::Value results_array(Json::arrayValue);
            for (const auto& item : results) {
                results_array.append(item.ToJson());
            }
            json["results"] = results_array;

            return json;
        }
    };

    /**
     * @brief 批量删除响应 DTO
     *
     * @details
     * 包含批量删除操作的摘要和每个项目的结果。
     */
    struct BatchDeleteResponse {
        BatchSummary summary;
        std::vector<BatchResultItem> results;

        /// 转换为 JSON
        [[nodiscard]]
        auto ToJson() const -> Json::Value {
            Json::Value json;
            json["summary"] = summary.ToJson();

            Json::Value results_array(Json::arrayValue);
            for (const auto& item : results) {
                results_array.append(item.ToJson());
            }
            json["results"] = results_array;

            return json;
        }
    };

    /**
     * @brief 清空回收站响应 DTO
     *
     * @details
     * 包含清空回收站的统计信息。
     */
    struct DeleteAllResponse {
        int deleted_count{ 0 };
        uint64_t freed_space{ 0 };

        /// 转换为 JSON
        [[nodiscard]]
        auto ToJson() const -> Json::Value {
            Json::Value json;
            json["deleted_count"] = deleted_count;
            json["freed_space"] = static_cast<Json::UInt64>(freed_space);
            return json;
        }
    };

} // namespace disk::trash
