/**
 * @file DtoBase.hpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief DTO 基类，提供 FromRequest/ToJson 通用辅助方法
 *
 * @copyright Copyright (c) 2026
 *
 * @details
 * CRTP 基类，为子类提供 protected 静态方法：
 * - JSON body 提取与必填/可选字段解析
 * - 查询参数解析（int/uint64）
 * - ToJson 辅助：SetField/SetOptional/SetArray
 */

#pragma once

#include <climits>
#include <concepts>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <drogon/HttpAppFramework.h>
#include <drogon/HttpRequest.h>
#include <json/json.h>

#include "utils/ErrorCode.hpp"

namespace disk {

template <typename Derived>
struct DtoBase {
protected:
    /// ==================== FromRequest Helpers ====================

    /// 提取 JSON body，无效时返回错误
    [[nodiscard]]
    static auto RequireJsonBody(const drogon::HttpRequestPtr& req)
        -> Result<std::shared_ptr<const Json::Value>> {
        const auto body = req->getBody();
        const auto& content_type = req->getHeader("content-type");
        if (body.empty() ||
            (req->getContentType() != drogon::CT_APPLICATION_JSON &&
             content_type.find("application/json") == std::string::npos)) {
            return std::unexpected(
                ErrorInfo(ErrorCode::ValidationFailed, "Request body is not valid JSON")
            );
        }

        // Drogon logs parser errors before request-aware validation can own the diagnostic.
        Json::CharReaderBuilder builder;
        builder["collectComments"] = false;
        builder["stackLimit"] = static_cast<Json::UInt>(drogon::app().getJsonParserStackLimit());
        auto json = std::make_shared<Json::Value>();
        std::string errors;
        const auto reader = std::unique_ptr<Json::CharReader>(builder.newCharReader());
        if (!reader->parse(body.data(), body.data() + body.size(), json.get(), &errors)) {
            return std::unexpected(
                ErrorInfo(ErrorCode::ValidationFailed, "Request body is not valid JSON")
            );
        }
        return std::shared_ptr<const Json::Value>(std::move(json));
    }

    /// 必填 string 字段：检查 isMember + isString
    [[nodiscard]]
    static auto RequireString(const Json::Value& json, const char* key) -> Result<std::string> {
        if (!json.isMember(key)) {
            return std::unexpected(ErrorInfo(
                ErrorCode::ValidationFailed,
                std::string("Missing required parameter: ") + key
            ));
        }
        if (!json[key].isString()) {
            return std::unexpected(ErrorInfo(
                ErrorCode::ValidationFailed,
                std::string("Parameter '") + key + "' type error: expected string"
            ));
        }
        return json[key].asString();
    }

    /// 必填 uint64 字段：检查 isMember + isIntegral
    [[nodiscard]]
    static auto RequireUInt64(const Json::Value& json, const char* key) -> Result<uint64_t> {
        if (!json.isMember(key)) {
            return std::unexpected(ErrorInfo(
                ErrorCode::ValidationFailed,
                std::string("Missing required parameter: ") + key
            ));
        }
        if (!json[key].isIntegral() || json[key].isInt64() && json[key].asInt64() < 0) {
            return std::unexpected(ErrorInfo(
                ErrorCode::ValidationFailed,
                std::string("Parameter '") + key + "' type error: expected unsigned integer"
            ));
        }
        return json[key].asUInt64();
    }

    /// 必填 int 字段：检查 isMember + isIntegral
    [[nodiscard]]
    static auto RequireInt(const Json::Value& json, const char* key) -> Result<int> {
        if (!json.isMember(key)) {
            return std::unexpected(ErrorInfo(
                ErrorCode::ValidationFailed,
                std::string("Missing required parameter: ") + key
            ));
        }
        if (!json[key].isIntegral()) {
            return std::unexpected(ErrorInfo(
                ErrorCode::ValidationFailed,
                std::string("Parameter '") + key + "' type error: expected integer"
            ));
        }
        return json[key].asInt();
    }

    /// 可选 string 字段，不存在返回 nullopt
    [[nodiscard]]
    static auto OptionalString(const Json::Value& json, const char* key)
        -> Result<std::optional<std::string>> {
        if (!json.isMember(key)) {
            return std::optional<std::string>(std::nullopt);
        }
        if (!json[key].isString()) {
            return std::unexpected(ErrorInfo(
                ErrorCode::ValidationFailed,
                std::string("Parameter '") + key + "' type error: expected string"
            ));
        }
        return json[key].asString();
    }

    /// 可选 uint64 字段
    [[nodiscard]]
    static auto OptionalUInt64(const Json::Value& json, const char* key)
        -> Result<std::optional<uint64_t>> {
        if (!json.isMember(key)) {
            return std::optional<uint64_t>(std::nullopt);
        }
        if (!json[key].isIntegral() || json[key].isInt64() && json[key].asInt64() < 0) {
            return std::unexpected(ErrorInfo(
                ErrorCode::ValidationFailed,
                std::string("Parameter '") + key + "' type error: expected unsigned integer"
            ));
        }
        return json[key].asUInt64();
    }

    /// 可选 bool 字段
    [[nodiscard]]
    static auto OptionalBool(const Json::Value& json, const char* key)
        -> Result<std::optional<bool>> {
        if (!json.isMember(key)) {
            return std::optional<bool>(std::nullopt);
        }
        if (!json[key].isBool()) {
            return std::unexpected(ErrorInfo(
                ErrorCode::ValidationFailed,
                std::string("Parameter '") + key + "' type error: expected boolean"
            ));
        }
        return json[key].asBool();
    }

    /// 解析正整数 uint64（用于路径参数）
    [[nodiscard]]
    static auto ParsePositiveUInt64(const std::string& str, const char* param_name)
        -> Result<uint64_t> {
        if (str.empty()) {
            return std::unexpected(ErrorInfo(
                ErrorCode::InvalidParameter,
                std::string("Parameter '") + param_name + "' is empty"
            ));
        }
        try {
            size_t pos = 0;
            auto value = std::stoull(str, &pos);
            if (pos != str.length()) {
                return std::unexpected(ErrorInfo(
                    ErrorCode::InvalidParameter,
                    std::string("Parameter '") + param_name + "' invalid format"
                ));
            }
            if (value == 0) {
                return std::unexpected(ErrorInfo(
                    ErrorCode::InvalidParameter,
                    std::string("Parameter '") + param_name + "' must be a positive integer"
                ));
            }
            return value;
        } catch (const std::exception&) {
            return std::unexpected(ErrorInfo(
                ErrorCode::InvalidParameter,
                std::string("Parameter '") + param_name + "' invalid format"
            ));
        }
    }

    /// 解析查询参数为正整数 int，不存在返回 nullopt
    [[nodiscard]]
    static auto QueryPositiveInt(
        const drogon::HttpRequestPtr& req,
        const char* key,
        int min = 1,
        int max = INT_MAX
    ) -> Result<std::optional<int>> {
        auto str = req->getParameter(key);
        if (str.empty()) {
            return std::optional<int>(std::nullopt);
        }
        try {
            size_t pos = 0;
            auto value = std::stoi(str, &pos);
            if (pos != str.length()) {
                return std::unexpected(ErrorInfo(
                    ErrorCode::ValidationFailed,
                    std::string("Parameter '") + key + "' invalid format"
                ));
            }
            if (value < min || value > max) {
                return std::unexpected(ErrorInfo(
                    ErrorCode::ValidationFailed,
                    std::string("Parameter '") + key + "' must be an integer between " +
                        std::to_string(min) + "-" + std::to_string(max)
                ));
            }
            return value;
        } catch (const std::exception&) {
            return std::unexpected(ErrorInfo(
                ErrorCode::ValidationFailed,
                std::string("Parameter '") + key + "' invalid format"
            ));
        }
    }

    /// 解析查询参数为 uint64，不存在返回 nullopt
    [[nodiscard]]
    static auto QueryUInt64(const drogon::HttpRequestPtr& req, const char* key)
        -> Result<std::optional<uint64_t>> {
        auto str = req->getParameter(key);
        if (str.empty()) {
            return std::optional<uint64_t>(std::nullopt);
        }
        try {
            size_t pos = 0;
            auto value = std::stoull(str, &pos);
            if (pos != str.length()) {
                return std::unexpected(ErrorInfo(
                    ErrorCode::ValidationFailed,
                    std::string("Parameter '") + key + "' invalid format"
                ));
            }
            return value;
        } catch (const std::exception&) {
            return std::unexpected(ErrorInfo(
                ErrorCode::ValidationFailed,
                std::string("Parameter '") + key + "' invalid format"
            ));
        }
    }

    /// 解析可选的正整数 ID 数组（不存在则跳过）
    [[nodiscard]]
    static auto OptionalPositiveIdArray(const Json::Value& json, const char* field)
        -> Result<std::vector<uint64_t>> {
        if (!json.isMember(field)) {
            return std::vector<uint64_t>{};
        }
        if (!json[field].isArray()) {
            return std::unexpected(ErrorInfo(
                ErrorCode::InvalidParameter,
                std::string("Parameter '") + field + "' type error: expected array"
            ));
        }
        std::vector<uint64_t> ids;
        const auto& arr = json[field];
        ids.reserve(arr.size());
        for (Json::ArrayIndex i = 0; i < arr.size(); ++i) {
            if (!arr[i].isIntegral()) {
                return std::unexpected(ErrorInfo(
                    ErrorCode::InvalidParameter,
                    std::string("Element in parameter '") + field +
                        "' type error: expected integer"
                ));
            }
            auto id = arr[i].asUInt64();
            if (id == 0) {
                return std::unexpected(ErrorInfo(
                    ErrorCode::InvalidParameter,
                    std::string("Element in parameter '") + field +
                        "' must be a positive integer"
                ));
            }
            ids.push_back(id);
        }
        return ids;
    }

    /// 解析必填的正整数 ID 数组
    [[nodiscard]]
    static auto RequirePositiveIdArray(
        const Json::Value& json,
        const char* field,
        size_t min_size = 1,
        size_t max_size = 100
    ) -> Result<std::vector<uint64_t>> {
        if (!json.isMember(field)) {
            return std::unexpected(ErrorInfo(
                ErrorCode::ValidationFailed,
                std::string("Missing required parameter: ") + field
            ));
        }
        if (!json[field].isArray()) {
            return std::unexpected(ErrorInfo(
                ErrorCode::ValidationFailed,
                std::string("Parameter '") + field + "' type error: expected array"
            ));
        }
        const auto& arr = json[field];
        if (arr.size() < min_size) {
            return std::unexpected(ErrorInfo(
                ErrorCode::ValidationFailed,
                std::string("Parameter '") + field + "' cannot be empty array"
            ));
        }
        if (arr.size() > max_size) {
            return std::unexpected(ErrorInfo(
                ErrorCode::ValidationFailed,
                std::string("Parameter '") + field + "' supports at most " +
                    std::to_string(max_size) + " IDs"
            ));
        }
        std::vector<uint64_t> ids;
        ids.reserve(arr.size());
        for (Json::ArrayIndex i = 0; i < arr.size(); ++i) {
            if (!arr[i].isIntegral()) {
                return std::unexpected(ErrorInfo(
                    ErrorCode::ValidationFailed,
                    std::string("Parameter '") + field + "[" + std::to_string(i) +
                        "]' type error: expected positive integer"
                ));
            }
            auto id = arr[i].asUInt64();
            if (id == 0) {
                return std::unexpected(ErrorInfo(
                    ErrorCode::ValidationFailed,
                    std::string("Parameter '") + field + "[" + std::to_string(i) +
                        "]' must be a positive integer"
                ));
            }
            ids.push_back(id);
        }
        return ids;
    }

    /// ==================== ToJson Helpers ====================

    /// 设置 uint64 字段（自动 UInt64 转换）
    static void SetField(Json::Value& json, const char* key, uint64_t value) {
        json[key] = static_cast<Json::UInt64>(value);
    }

    /// 设置 int64 字段（自动 Int64 转换）
    static void SetField(Json::Value& json, const char* key, int64_t value) {
        json[key] = static_cast<Json::Int64>(value);
    }

    /// 设置 string 字段
    static void SetField(Json::Value& json, const char* key, const std::string& value) {
        json[key] = value;
    }

    /// 设置 int 字段
    static void SetField(Json::Value& json, const char* key, int value) {
        json[key] = value;
    }

    /// 设置 bool 字段
    static void SetField(Json::Value& json, const char* key, bool value) {
        json[key] = value;
    }

    /// 设置 double 字段
    static void SetField(Json::Value& json, const char* key, double value) {
        json[key] = value;
    }

    /// 设置 uint32 字段
    static void SetField(Json::Value& json, const char* key, uint32_t value) {
        json[key] = value;
    }

    /// 设置嵌套对象（调用 .ToJson()）
    template <typename T>
    static void SetField(Json::Value& json, const char* key, const T& value)
        requires requires(const T& v) {
            { v.ToJson() } -> std::same_as<Json::Value>;
        }
    {
        json[key] = value.ToJson();
    }

    /// 设置可选字段（有值设置，无值跳过）
    template <typename T>
    static void SetOptional(Json::Value& json, const char* key, const std::optional<T>& opt) {
        if (opt.has_value()) {
            SetField(json, key, *opt);
        }
    }

    /// 设置可选字段（有值设置，无值设 null）
    template <typename T>
    static void SetOptionalOrNull(
        Json::Value& json,
        const char* key,
        const std::optional<T>& opt
    ) {
        if (opt.has_value()) {
            SetField(json, key, *opt);
        } else {
            json[key] = Json::nullValue;
        }
    }

    /// 设置对象数组（调用每个元素的 .ToJson()）
    template <typename T>
    static void SetArray(Json::Value& json, const char* key, const std::vector<T>& items)
        requires requires(const T& v) {
            { v.ToJson() } -> std::same_as<Json::Value>;
        }
    {
        Json::Value arr(Json::arrayValue);
        arr.resize(items.size());
        for (Json::ArrayIndex i = 0; i < items.size(); ++i) {
            arr[i] = items[i].ToJson();
        }
        json[key] = arr;
    }

    /// 设置 uint32 数组
    static void SetArray(Json::Value& json, const char* key, const std::vector<uint32_t>& items) {
        Json::Value arr(Json::arrayValue);
        arr.resize(items.size());
        for (Json::ArrayIndex i = 0; i < items.size(); ++i) {
            arr[i] = items[i];
        }
        json[key] = arr;
    }
};

} ///< namespace disk
