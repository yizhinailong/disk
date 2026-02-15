/**
 * @file FileDto.hpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 文件模块数据传输对象
 * @version 0.1
 * @date 2026-02-14
 *
 * @copyright Copyright (c) 2026
 *
 * @details
 * 本文件包含文件上传模块的所有数据传输对象（DTO）：
 * - InitUploadRequest: 初始化上传请求
 * - InitUploadResponse: 初始化上传响应
 * - UploadChunkRequest: 上传分片请求
 * - UploadChunkResponse: 上传分片响应
 * - CompleteUploadRequest: 完成上传请求
 * - CompleteUploadResponse: 完成上传响应
 * - FileItem: 文件项（共享响应组件）
 *
 * DTO 用于在不同层（Controller、Service）之间传输数据，
 * 包含请求验证和响应序列化逻辑。
 */

#pragma once

#include <algorithm>
#include <optional>
#include <string>
#include <vector>

#include <drogon/HttpRequest.h>
#include <json/json.h>

#include "utils/ErrorCode.hpp"

namespace disk::file {

    // ==================== FileItem (shared response component) ====================

    /**
     * @brief 文件项数据
     *
     * @details
     * 用于表示单个文件的基本信息。
     * 可在多种响应中复用（如列表、详情、上传完成等）。
     */
    struct FileItem {
        uint64_t id;
        std::string name;
        uint64_t size;
        std::string hash;
        std::string mime_type;
        uint64_t parent_id;
        std::string created_at;

        /// 转换为 JSON
        [[nodiscard]]
        auto ToJson() const -> Json::Value {
            Json::Value json;
            json["id"] = static_cast<Json::UInt64>(id);
            json["name"] = name;
            json["size"] = static_cast<Json::UInt64>(size);
            json["hash"] = hash;
            json["mime_type"] = mime_type;
            json["parent_id"] = static_cast<Json::UInt64>(parent_id);
            json["created_at"] = created_at;
            return json;
        }
    };

    // ==================== Init Upload ====================

    /**
     * @brief 初始化上传请求 DTO
     *
     * @details
     * 验证规则：
     * - filename: 1-255字符，禁止字符 / \ : * ? " < > | 及控制字符
     * - filename: 禁止保留名称 "." 和 ".."
     * - filename: 禁止以 "." 开头（隐藏文件）
     * - filename: 仅允许 ASCII 可打印字符 (0x20-0x7E)
     * - file_size: 必须 > 0（最大文件大小由服务层验证）
     * - file_hash: 32字符的十六进制字符串（MD5）
     * - parent_id: 默认 0（根目录）
     */
    struct InitUploadRequest {
        std::string filename;
        uint64_t file_size{ 0 };
        std::string file_hash;
        uint64_t parent_id{ 0 };

        /// 从 HTTP 请求解析并验证，返回 Result
        [[nodiscard]]
        static auto FromRequest(const drogon::HttpRequestPtr& req) -> Result<InitUploadRequest> {
            LOG_DEBUG << "开始解析初始化上传请求参数";

            auto json_ptr = req->getJsonObject();
            if (!json_ptr) {
                LOG_WARN << "请求体不是有效的 JSON";
                return std::unexpected(ErrorInfo(ErrorCode::ValidationFailed, "请求体不是有效的 JSON"));
            }

            const auto& json = *json_ptr;

            // 检查必填字段
            if (!json.isMember("filename")) {
                LOG_WARN << "缺少必需参数: filename";
                return std::unexpected(ErrorInfo(ErrorCode::ValidationFailed, "缺少必需参数: filename"));
            }
            if (!json.isMember("file_size")) {
                LOG_WARN << "缺少必需参数: file_size";
                return std::unexpected(ErrorInfo(ErrorCode::ValidationFailed, "缺少必需参数: file_size"));
            }
            if (!json.isMember("file_hash")) {
                LOG_WARN << "缺少必需参数: file_hash";
                return std::unexpected(ErrorInfo(ErrorCode::ValidationFailed, "缺少必需参数: file_hash"));
            }

            // 检查字段类型
            if (!json["filename"].isString()) {
                LOG_WARN << "参数 'filename' 类型错误: 期望字符串";
                return std::unexpected(ErrorInfo(ErrorCode::ValidationFailed, "参数 'filename' 类型错误: 期望字符串"));
            }
            if (!json["file_size"].isIntegral()) {
                LOG_WARN << "参数 'file_size' 类型错误: 期望整数";
                return std::unexpected(ErrorInfo(ErrorCode::ValidationFailed, "参数 'file_size' 类型错误: 期望整数"));
            }
            if (!json["file_hash"].isString()) {
                LOG_WARN << "参数 'file_hash' 类型错误: 期望字符串";
                return std::unexpected(ErrorInfo(ErrorCode::ValidationFailed, "参数 'file_hash' 类型错误: 期望字符串"));
            }

            InitUploadRequest request;
            request.filename = json["filename"].asString();
            request.file_size = json["file_size"].asUInt64();
            request.file_hash = json["file_hash"].asString();

            // 处理可选参数 parent_id
            if (json.isMember("parent_id")) {
                if (!json["parent_id"].isIntegral()) {
                    LOG_WARN << "参数 'parent_id' 类型错误: 期望整数";
                    return std::unexpected(ErrorInfo(ErrorCode::ValidationFailed, "参数 'parent_id' 类型错误: 期望整数"));
                }
                request.parent_id = json["parent_id"].asUInt64();
            }

            LOG_DEBUG << "解析到初始化上传请求: filename=\"" << request.filename
                      << "\", file_size=" << request.file_size
                      << ", file_hash=" << request.file_hash
                      << ", parent_id=" << request.parent_id;

            // 验证文件名
            if (!request.ValidateFilenameLength()) {
                LOG_WARN << "文件名长度无效: " << request.filename.length();
                return std::unexpected(ErrorInfo(ErrorCode::ValidationFailed, "文件名长度必须在 1-255 字符之间"));
            }

            if (!request.ValidateFilenameForbiddenChars()) {
                LOG_WARN << "文件名包含禁止字符: " << request.filename;
                return std::unexpected(ErrorInfo(ErrorCode::InvalidFilename, "文件名包含禁止字符：/ \\ : * ? \" < > | 或控制字符"));
            }

            if (!request.ValidateFilenameReservedNames()) {
                LOG_WARN << "文件名为保留名称: " << request.filename;
                return std::unexpected(ErrorInfo(ErrorCode::InvalidFilename, "文件名不能为保留名称 \".\" 或 \"..\""));
            }

            if (!request.ValidateFilenameNotHidden()) {
                LOG_WARN << "文件名以点开头（隐藏文件）: " << request.filename;
                return std::unexpected(ErrorInfo(ErrorCode::InvalidFilename, "文件名不能以 \".\" 开头"));
            }

            if (!request.ValidateFilenameCharset()) {
                LOG_WARN << "文件名包含非 ASCII 字符: " << request.filename;
                return std::unexpected(ErrorInfo(ErrorCode::InvalidFilename, "文件名仅允许 ASCII 可打印字符"));
            }

            // 验证文件大小
            if (!request.ValidateFileSize()) {
                LOG_WARN << "文件大小无效: " << request.file_size;
                return std::unexpected(ErrorInfo(ErrorCode::ValidationFailed, "文件大小必须大于 0 且不超过最大限制"));
            }

            // 验证文件哈希
            if (!request.ValidateFileHash()) {
                LOG_WARN << "文件哈希格式错误: " << request.file_hash;
                return std::unexpected(ErrorInfo(ErrorCode::ValidationFailed, "文件哈希必须是 32 位小写十六进制字符串"));
            }

            LOG_DEBUG << "请求参数验证通过";
            return request;
        }

    private:
        /// 验证文件名长度 (1-255 字符)
        [[nodiscard]]
        auto ValidateFilenameLength() const -> bool {
            return filename.length() >= 1 && filename.length() <= 255;
        }

        /// 验证文件名禁止字符 (/ \ : * ? " < > | 及控制字符 0x00-0x1F)
        [[nodiscard]]
        auto ValidateFilenameForbiddenChars() const -> bool {
            static const char forbidden_chars[] = "/\\:*?\"<>|";
            for (char c : filename) {
                // 检查控制字符 (0x00-0x1F)
                if (static_cast<unsigned char>(c) <= 0x1F) {
                    return false;
                }
                // 检查文件系统保留字符
                for (char fc : forbidden_chars) {
                    if (c == fc) {
                        return false;
                    }
                }
            }
            return true;
        }

        /// 验证文件名保留名称 (. 和 ..)
        [[nodiscard]]
        auto ValidateFilenameReservedNames() const -> bool {
            return filename != "." && filename != "..";
        }

        /// 验证文件名不以点开头（非隐藏文件）
        [[nodiscard]]
        auto ValidateFilenameNotHidden() const -> bool {
            return filename.empty() || filename[0] != '.';
        }

        /// 验证文件名字符集（仅 ASCII 可打印字符 0x20-0x7E）
        [[nodiscard]]
        auto ValidateFilenameCharset() const -> bool {
            for (char c : filename) {
                // ASCII 可打印字符范围: 0x20 (空格) 到 0x7E (~)
                if (static_cast<unsigned char>(c) < 0x20 || static_cast<unsigned char>(c) > 0x7E) {
                    return false;
                }
            }
            return true;
        }

        /// 验证文件大小 (> 0)
        /// 注意：最大文件大小验证由服务层完成，DTO 层仅验证非零
        [[nodiscard]]
        auto ValidateFileSize() const -> bool {
            return file_size > 0;
        }

        /// 验证文件哈希 (32-char lowercase hex MD5)
        [[nodiscard]]
        auto ValidateFileHash() const -> bool {
            if (file_hash.length() != 32) {
                return false;
            }
            for (char c : file_hash) {
                if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) {
                    return false;
                }
            }
            return true;
        }
    };

    /**
     * @brief 初始化上传响应 DTO
     *
     * @details
     * 包含上传会话信息或秒传结果。
     * - 正常上传: 返回 upload_id, chunk_size, total_chunks
     * - 秒传: instant_upload=true, 返回 file 对象
     * - 断点续传: 返回已上传的分片列表
     */
    struct InitUploadResponse {
        std::string upload_id;                 ///< 上传会话ID（秒传时为空）
        uint32_t chunk_size{ 0 };              ///< 分片大小
        uint32_t total_chunks{ 0 };            ///< 总分片数
        std::vector<uint32_t> uploaded_chunks; ///< 已上传的分片索引（用于断点续传）
        bool instant_upload{ false };          ///< 是否秒传
        std::optional<FileItem> file;          ///< 秒传时的文件信息

        /// 转换为 JSON
        [[nodiscard]]
        auto ToJson() const -> Json::Value {
            Json::Value json;
            json["upload_id"] = upload_id;
            json["chunk_size"] = chunk_size;
            json["total_chunks"] = total_chunks;

            Json::Value chunks_array(Json::arrayValue);
            for (const auto& chunk : uploaded_chunks) {
                chunks_array.append(chunk);
            }
            json["uploaded_chunks"] = chunks_array;

            json["instant_upload"] = instant_upload;

            if (file.has_value()) {
                json["file"] = file->ToJson();
            }

            return json;
        }
    };

    // ==================== Upload Chunk ====================

    /**
     * @brief 上传分片请求 DTO
     *
     * @details
     * 验证规则：
     * - upload_id: 非空字符串
     * - chunk_index: 非负整数
     * - chunk_hash: 32字符的十六进制字符串（MD5）
     *
     * 注意：实际分片数据通过 multipart/form-data 上传，
     * 此 DTO 仅包含元数据验证。
     */
    struct UploadChunkRequest {
        std::string upload_id;
        uint32_t chunk_index{ 0 };
        std::string chunk_hash;

        /// 从 HTTP 请求解析并验证，返回 Result
        [[nodiscard]]
        static auto FromRequest(const drogon::HttpRequestPtr& req) -> Result<UploadChunkRequest> {
            LOG_DEBUG << "开始解析上传分片请求参数";

            auto json_ptr = req->getJsonObject();
            if (!json_ptr) {
                LOG_WARN << "请求体不是有效的 JSON";
                return std::unexpected(ErrorInfo(ErrorCode::ValidationFailed, "请求体不是有效的 JSON"));
            }

            const auto& json = *json_ptr;

            // 检查必填字段
            if (!json.isMember("upload_id")) {
                LOG_WARN << "缺少必需参数: upload_id";
                return std::unexpected(ErrorInfo(ErrorCode::ValidationFailed, "缺少必需参数: upload_id"));
            }
            if (!json.isMember("chunk_hash")) {
                LOG_WARN << "缺少必需参数: chunk_hash";
                return std::unexpected(ErrorInfo(ErrorCode::ValidationFailed, "缺少必需参数: chunk_hash"));
            }

            // 检查字段类型
            if (!json["upload_id"].isString()) {
                LOG_WARN << "参数 'upload_id' 类型错误: 期望字符串";
                return std::unexpected(ErrorInfo(ErrorCode::ValidationFailed, "参数 'upload_id' 类型错误: 期望字符串"));
            }
            if (!json["chunk_hash"].isString()) {
                LOG_WARN << "参数 'chunk_hash' 类型错误: 期望字符串";
                return std::unexpected(ErrorInfo(ErrorCode::ValidationFailed, "参数 'chunk_hash' 类型错误: 期望字符串"));
            }

            UploadChunkRequest request;
            request.upload_id = json["upload_id"].asString();
            request.chunk_hash = json["chunk_hash"].asString();

            // 处理可选参数 chunk_index
            if (json.isMember("chunk_index")) {
                if (!json["chunk_index"].isIntegral()) {
                    LOG_WARN << "参数 'chunk_index' 类型错误: 期望整数";
                    return std::unexpected(ErrorInfo(ErrorCode::ValidationFailed, "参数 'chunk_index' 类型错误: 期望整数"));
                }
                request.chunk_index = json["chunk_index"].asUInt();
            }

            LOG_DEBUG << "解析到上传分片请求: upload_id=" << request.upload_id
                      << ", chunk_index=" << request.chunk_index
                      << ", chunk_hash=" << request.chunk_hash;

            // 验证 upload_id 非空
            if (request.upload_id.empty()) {
                LOG_WARN << "upload_id 不能为空";
                return std::unexpected(ErrorInfo(ErrorCode::ValidationFailed, "upload_id 不能为空"));
            }

            // 验证分片哈希
            if (!request.ValidateChunkHash()) {
                LOG_WARN << "分片哈希格式错误: " << request.chunk_hash;
                return std::unexpected(ErrorInfo(ErrorCode::ValidationFailed, "分片哈希必须是 32 位小写十六进制字符串"));
            }

            LOG_DEBUG << "请求参数验证通过";
            return request;
        }

    private:
        /// 验证分片哈希 (32-char lowercase hex MD5)
        [[nodiscard]]
        auto ValidateChunkHash() const -> bool {
            if (chunk_hash.length() != 32) {
                return false;
            }
            for (char c : chunk_hash) {
                if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) {
                    return false;
                }
            }
            return true;
        }
    };

    /**
     * @brief 上传分片响应 DTO
     *
     * @details
     * 包含分片上传结果。
     */
    struct UploadChunkResponse {
        uint32_t chunk_index{ 0 }; ///< 分片索引
        bool uploaded{ false };    ///< 是否上传成功

        /// 转换为 JSON
        [[nodiscard]]
        auto ToJson() const -> Json::Value {
            Json::Value json;
            json["chunk_index"] = chunk_index;
            json["uploaded"] = uploaded;
            return json;
        }
    };

    // ==================== Complete Upload ====================

    /**
     * @brief 完成上传请求 DTO
     *
     * @details
     * 验证规则：
     * - upload_id: 非空字符串
     */
    struct CompleteUploadRequest {
        std::string upload_id;

        /// 从 HTTP 请求解析并验证，返回 Result
        [[nodiscard]]
        static auto FromRequest(const drogon::HttpRequestPtr& req) -> Result<CompleteUploadRequest> {
            LOG_DEBUG << "开始解析完成上传请求参数";

            auto json_ptr = req->getJsonObject();
            if (!json_ptr) {
                LOG_WARN << "请求体不是有效的 JSON";
                return std::unexpected(ErrorInfo(ErrorCode::ValidationFailed, "请求体不是有效的 JSON"));
            }

            const auto& json = *json_ptr;

            // 检查必填字段
            if (!json.isMember("upload_id")) {
                LOG_WARN << "缺少必需参数: upload_id";
                return std::unexpected(ErrorInfo(ErrorCode::ValidationFailed, "缺少必需参数: upload_id"));
            }

            // 检查字段类型
            if (!json["upload_id"].isString()) {
                LOG_WARN << "参数 'upload_id' 类型错误: 期望字符串";
                return std::unexpected(ErrorInfo(ErrorCode::ValidationFailed, "参数 'upload_id' 类型错误: 期望字符串"));
            }

            CompleteUploadRequest request;
            request.upload_id = json["upload_id"].asString();

            LOG_DEBUG << "解析到完成上传请求: upload_id=" << request.upload_id;

            // 验证 upload_id 非空
            if (request.upload_id.empty()) {
                LOG_WARN << "upload_id 不能为空";
                return std::unexpected(ErrorInfo(ErrorCode::ValidationFailed, "upload_id 不能为空"));
            }

            LOG_DEBUG << "请求参数验证通过";
            return request;
        }
    };

    /**
     * @brief 完成上传响应 DTO
     *
     * @details
     * 包含上传完成后的文件信息。
     */
    struct CompleteUploadResponse {
        FileItem file; ///< 文件信息

        /// 转换为 JSON
        [[nodiscard]]
        auto ToJson() const -> Json::Value {
            Json::Value json;
            json["file"] = file.ToJson();
            return json;
        }
    };

} // namespace disk::file
