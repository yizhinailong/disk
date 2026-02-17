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
 * 本文件包含文件模块的所有数据传输对象（DTO）：
 * - FileItem: 文件项（共享响应组件）
 * - InitUploadRequest: 初始化上传请求
 * - InitUploadResponse: 初始化上传响应
 * - UploadChunkRequest: 上传分片请求
 * - UploadChunkResponse: 上传分片响应
 * - CompleteUploadRequest: 完成上传请求
 * - CompleteUploadResponse: 完成上传响应
 * - FileListRequest: 获取文件列表请求
 * - FileListItem: 文件列表项
 * - FileListResponse: 获取文件列表响应
 * - DownloadInfoRequest: 获取下载信息请求
 * - DownloadInfoResponse: 获取下载信息响应
 * - DownloadRequest: 下载文件请求
 * - RenameRequest: 重命名请求
 * - RenameResponse: 重命名响应
 * - MoveRequest: 移动文件请求
 * - MoveResponse: 移动文件响应
 * - CopyRequest: 复制文件请求
 * - CopyResponse: 复制文件响应
 * - FileIdMapping: 文件ID映射
 * - DeleteRequest: 删除文件请求
 * - DeleteResponse: 删除文件响应
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
#include <drogon/HttpResponse.h>
#include <json/json.h>

#include "utils/ErrorCode.hpp"
#include "utils/Response.hpp"

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
                return std::unexpected(
                    ErrorInfo(ErrorCode::ValidationFailed, "请求体不是有效的 JSON")
                );
            }

            const auto& json = *json_ptr;

            // 检查必填字段
            if (!json.isMember("filename")) {
                LOG_WARN << "缺少必需参数: filename";
                return std::unexpected(
                    ErrorInfo(ErrorCode::ValidationFailed, "缺少必需参数: filename")
                );
            }
            if (!json.isMember("file_size")) {
                LOG_WARN << "缺少必需参数: file_size";
                return std::unexpected(
                    ErrorInfo(ErrorCode::ValidationFailed, "缺少必需参数: file_size")
                );
            }
            if (!json.isMember("file_hash")) {
                LOG_WARN << "缺少必需参数: file_hash";
                return std::unexpected(
                    ErrorInfo(ErrorCode::ValidationFailed, "缺少必需参数: file_hash")
                );
            }

            // 检查字段类型
            if (!json["filename"].isString()) {
                LOG_WARN << "参数 'filename' 类型错误: 期望字符串";
                return std::unexpected(
                    ErrorInfo(ErrorCode::ValidationFailed, "参数 'filename' 类型错误: 期望字符串")
                );
            }
            if (!json["file_size"].isIntegral()) {
                LOG_WARN << "参数 'file_size' 类型错误: 期望整数";
                return std::unexpected(
                    ErrorInfo(ErrorCode::ValidationFailed, "参数 'file_size' 类型错误: 期望整数")
                );
            }
            if (!json["file_hash"].isString()) {
                LOG_WARN << "参数 'file_hash' 类型错误: 期望字符串";
                return std::unexpected(
                    ErrorInfo(ErrorCode::ValidationFailed, "参数 'file_hash' 类型错误: 期望字符串")
                );
            }

            InitUploadRequest request;
            request.filename = json["filename"].asString();
            request.file_size = json["file_size"].asUInt64();
            request.file_hash = json["file_hash"].asString();

            // 处理可选参数 parent_id
            if (json.isMember("parent_id")) {
                if (!json["parent_id"].isIntegral()) {
                    LOG_WARN << "参数 'parent_id' 类型错误: 期望整数";
                    return std::unexpected(ErrorInfo(
                        ErrorCode::ValidationFailed,
                        "参数 'parent_id' 类型错误: 期望整数"
                    ));
                }
                request.parent_id = json["parent_id"].asUInt64();
            }

            LOG_DEBUG << "解析到初始化上传请求: filename=\"" << request.filename
                      << "\", file_size=" << request.file_size
                      << ", file_hash=" << request.file_hash << ", parent_id=" << request.parent_id;

            // 验证文件名
            if (!request.ValidateFilenameLength()) {
                LOG_WARN << "文件名长度无效: " << request.filename.length();
                return std::unexpected(
                    ErrorInfo(ErrorCode::ValidationFailed, "文件名长度必须在 1-255 字符之间")
                );
            }

            if (!request.ValidateFilenameForbiddenChars()) {
                LOG_WARN << "文件名包含禁止字符: " << request.filename;
                return std::unexpected(ErrorInfo(
                    ErrorCode::InvalidFilename,
                    "文件名包含禁止字符：/ \\ : * ? \" < > | 或控制字符"
                ));
            }

            if (!request.ValidateFilenameReservedNames()) {
                LOG_WARN << "文件名为保留名称: " << request.filename;
                return std::unexpected(
                    ErrorInfo(ErrorCode::InvalidFilename, "文件名不能为保留名称 \".\" 或 \"..\"")
                );
            }

            if (!request.ValidateFilenameNotHidden()) {
                LOG_WARN << "文件名以点开头（隐藏文件）: " << request.filename;
                return std::unexpected(
                    ErrorInfo(ErrorCode::InvalidFilename, "文件名不能以 \".\" 开头")
                );
            }

            if (!request.ValidateFilenameCharset()) {
                LOG_WARN << "文件名包含非 ASCII 字符: " << request.filename;
                return std::unexpected(
                    ErrorInfo(ErrorCode::InvalidFilename, "文件名仅允许 ASCII 可打印字符")
                );
            }

            // 验证文件大小
            if (!request.ValidateFileSize()) {
                LOG_WARN << "文件大小无效: " << request.file_size;
                return std::unexpected(
                    ErrorInfo(ErrorCode::ValidationFailed, "文件大小必须大于 0 且不超过最大限制")
                );
            }

            // 验证文件哈希
            if (!request.ValidateFileHash()) {
                LOG_WARN << "文件哈希格式错误: " << request.file_hash;
                return std::unexpected(
                    ErrorInfo(ErrorCode::ValidationFailed, "文件哈希必须是 32 位小写十六进制字符串")
                );
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
                return std::unexpected(
                    ErrorInfo(ErrorCode::ValidationFailed, "请求体不是有效的 JSON")
                );
            }

            const auto& json = *json_ptr;

            // 检查必填字段
            if (!json.isMember("upload_id")) {
                LOG_WARN << "缺少必需参数: upload_id";
                return std::unexpected(
                    ErrorInfo(ErrorCode::ValidationFailed, "缺少必需参数: upload_id")
                );
            }
            if (!json.isMember("chunk_hash")) {
                LOG_WARN << "缺少必需参数: chunk_hash";
                return std::unexpected(
                    ErrorInfo(ErrorCode::ValidationFailed, "缺少必需参数: chunk_hash")
                );
            }

            // 检查字段类型
            if (!json["upload_id"].isString()) {
                LOG_WARN << "参数 'upload_id' 类型错误: 期望字符串";
                return std::unexpected(
                    ErrorInfo(ErrorCode::ValidationFailed, "参数 'upload_id' 类型错误: 期望字符串")
                );
            }
            if (!json["chunk_hash"].isString()) {
                LOG_WARN << "参数 'chunk_hash' 类型错误: 期望字符串";
                return std::unexpected(
                    ErrorInfo(ErrorCode::ValidationFailed, "参数 'chunk_hash' 类型错误: 期望字符串")
                );
            }

            UploadChunkRequest request;
            request.upload_id = json["upload_id"].asString();
            request.chunk_hash = json["chunk_hash"].asString();

            // 处理可选参数 chunk_index
            if (json.isMember("chunk_index")) {
                if (!json["chunk_index"].isIntegral()) {
                    LOG_WARN << "参数 'chunk_index' 类型错误: 期望整数";
                    return std::unexpected(ErrorInfo(
                        ErrorCode::ValidationFailed,
                        "参数 'chunk_index' 类型错误: 期望整数"
                    ));
                }
                request.chunk_index = json["chunk_index"].asUInt();
            }

            LOG_DEBUG << "解析到上传分片请求: upload_id=" << request.upload_id
                      << ", chunk_index=" << request.chunk_index
                      << ", chunk_hash=" << request.chunk_hash;

            // 验证 upload_id 非空
            if (request.upload_id.empty()) {
                LOG_WARN << "upload_id 不能为空";
                return std::unexpected(
                    ErrorInfo(ErrorCode::ValidationFailed, "upload_id 不能为空")
                );
            }

            // 验证分片哈希
            if (!request.ValidateChunkHash()) {
                LOG_WARN << "分片哈希格式错误: " << request.chunk_hash;
                return std::unexpected(
                    ErrorInfo(ErrorCode::ValidationFailed, "分片哈希必须是 32 位小写十六进制字符串")
                );
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
        static auto FromRequest(const drogon::HttpRequestPtr& req)
            -> Result<CompleteUploadRequest> {
            LOG_DEBUG << "开始解析完成上传请求参数";

            auto json_ptr = req->getJsonObject();
            if (!json_ptr) {
                LOG_WARN << "请求体不是有效的 JSON";
                return std::unexpected(
                    ErrorInfo(ErrorCode::ValidationFailed, "请求体不是有效的 JSON")
                );
            }

            const auto& json = *json_ptr;

            // 检查必填字段
            if (!json.isMember("upload_id")) {
                LOG_WARN << "缺少必需参数: upload_id";
                return std::unexpected(
                    ErrorInfo(ErrorCode::ValidationFailed, "缺少必需参数: upload_id")
                );
            }

            // 检查字段类型
            if (!json["upload_id"].isString()) {
                LOG_WARN << "参数 'upload_id' 类型错误: 期望字符串";
                return std::unexpected(
                    ErrorInfo(ErrorCode::ValidationFailed, "参数 'upload_id' 类型错误: 期望字符串")
                );
            }

            CompleteUploadRequest request;
            request.upload_id = json["upload_id"].asString();

            LOG_DEBUG << "解析到完成上传请求: upload_id=" << request.upload_id;

            // 验证 upload_id 非空
            if (request.upload_id.empty()) {
                LOG_WARN << "upload_id 不能为空";
                return std::unexpected(
                    ErrorInfo(ErrorCode::ValidationFailed, "upload_id 不能为空")
                );
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

    // ==================== File List ====================

    /**
     * @brief 获取文件列表请求 DTO
     *
     * @details
     * 验证规则：
     * - parent_id: 默认 0（根目录）
     * - page: 默认 1，必须 > 0
     * - page_size: 默认 20，必须 > 0 且 <= 100
     * - sort_by: 默认 name，可选值 name/size/created_at/updated_at
     * - sort_order: 默认 asc，可选值 asc/desc
     * - type: 默认 all，可选值 all/file/folder
     *
     * 从 URL 查询参数解析。
     */
    struct FileListRequest {
        uint64_t parent_id{ 0 };
        int page{ 1 };
        int page_size{ 20 };
        std::string sort_by{ "name" };
        std::string sort_order{ "asc" };
        std::string type{ "all" };

        /// 从 HTTP 请求查询参数解析并验证，返回 Result
        [[nodiscard]]
        static auto FromRequest(const drogon::HttpRequestPtr& req) -> Result<FileListRequest> {
            LOG_DEBUG << "开始解析文件列表请求参数";

            FileListRequest request;

            // 有效排序字段
            static const std::set<std::string> valid_sort_by = { "name",
                                                                 "size",
                                                                 "created_at",
                                                                 "updated_at" };

            // 有效排序方向
            static const std::set<std::string> valid_sort_order = { "asc", "desc" };

            // 有效类型
            static const std::set<std::string> valid_types = { "all", "file", "folder" };

            // 解析可选参数 parent_id
            auto parent_id_str = req->getParameter("parent_id");
            if (!parent_id_str.empty()) {
                try {
                    size_t pos = 0;
                    auto value = std::stoull(parent_id_str, &pos);
                    if (pos != parent_id_str.length()) {
                        LOG_WARN << "参数 'parent_id' 格式无效: " << parent_id_str;
                        return std::unexpected(
                            ErrorInfo(ErrorCode::ValidationFailed, "参数 'parent_id' 格式无效")
                        );
                    }
                    request.parent_id = value;
                } catch (const std::exception& e) {
                    LOG_WARN << "参数 'parent_id' 格式无效: " << parent_id_str;
                    return std::unexpected(
                        ErrorInfo(ErrorCode::ValidationFailed, "参数 'parent_id' 格式无效")
                    );
                }
            }

            // 解析可选参数 page
            auto page_str = req->getParameter("page");
            if (!page_str.empty()) {
                try {
                    size_t pos = 0;
                    auto value = std::stoi(page_str, &pos);
                    if (pos != page_str.length() || value <= 0) {
                        LOG_WARN << "参数 'page' 格式无效或值无效: " << page_str;
                        return std::unexpected(
                            ErrorInfo(ErrorCode::ValidationFailed, "参数 'page' 必须是正整数")
                        );
                    }
                    request.page = value;
                } catch (const std::exception& e) {
                    LOG_WARN << "参数 'page' 格式无效: " << page_str;
                    return std::unexpected(
                        ErrorInfo(ErrorCode::ValidationFailed, "参数 'page' 格式无效")
                    );
                }
            }

            // 解析可选参数 page_size
            auto page_size_str = req->getParameter("page_size");
            if (!page_size_str.empty()) {
                try {
                    size_t pos = 0;
                    auto value = std::stoi(page_size_str, &pos);
                    if (pos != page_size_str.length() || value <= 0 || value > 100) {
                        LOG_WARN << "参数 'page_size' 格式无效或值无效: " << page_size_str;
                        return std::unexpected(ErrorInfo(
                            ErrorCode::ValidationFailed,
                            "参数 'page_size' 必须是 1-100 之间的整数"
                        ));
                    }
                    request.page_size = value;
                } catch (const std::exception& e) {
                    LOG_WARN << "参数 'page_size' 格式无效: " << page_size_str;
                    return std::unexpected(
                        ErrorInfo(ErrorCode::ValidationFailed, "参数 'page_size' 格式无效")
                    );
                }
            }

            // 解析可选参数 sort_by
            auto sort_by_str = req->getParameter("sort_by");
            if (!sort_by_str.empty()) {
                if (valid_sort_by.find(sort_by_str) == valid_sort_by.end()) {
                    LOG_WARN << "参数 'sort_by' 值无效: " << sort_by_str;
                    return std::unexpected(ErrorInfo(
                        ErrorCode::ValidationFailed,
                        "参数 'sort_by' 值无效，必须是 name/size/created_at/updated_at"
                    ));
                }
                request.sort_by = sort_by_str;
            }

            // 解析可选参数 sort_order
            auto sort_order_str = req->getParameter("sort_order");
            if (!sort_order_str.empty()) {
                if (valid_sort_order.find(sort_order_str) == valid_sort_order.end()) {
                    LOG_WARN << "参数 'sort_order' 值无效: " << sort_order_str;
                    return std::unexpected(ErrorInfo(
                        ErrorCode::ValidationFailed,
                        "参数 'sort_order' 值无效，必须是 asc/desc"
                    ));
                }
                request.sort_order = sort_order_str;
            }

            // 解析可选参数 type
            auto type_str = req->getParameter("type");
            if (!type_str.empty()) {
                if (valid_types.find(type_str) == valid_types.end()) {
                    LOG_WARN << "参数 'type' 值无效: " << type_str;
                    return std::unexpected(ErrorInfo(
                        ErrorCode::ValidationFailed,
                        "参数 'type' 值无效，必须是 all/file/folder"
                    ));
                }
                request.type = type_str;
            }

            LOG_DEBUG << "解析到文件列表请求: parent_id=" << request.parent_id
                      << ", page=" << request.page << ", page_size=" << request.page_size
                      << ", sort_by=" << request.sort_by << ", sort_order=" << request.sort_order
                      << ", type=" << request.type;

            return request;
        }
    };

    /**
     * @brief 文件列表项
     *
     * @details
     * 用于表示文件列表中的单个文件或文件夹项。
     */
    struct FileListItem {
        uint64_t id;
        std::string name;
        std::string type; ///< "file" 或 "folder"
        // For files:
        uint64_t size{ 0 };
        std::string mime_type;
        std::string hash;
        // For folders:
        int item_count{ 0 };
        // Common:
        std::string created_at;
        std::string updated_at;

        /// 转换为 JSON
        [[nodiscard]]
        auto ToJson() const -> Json::Value {
            Json::Value json;
            json["id"] = static_cast<Json::UInt64>(id);
            json["name"] = name;
            json["type"] = type;

            if (type == "file") {
                json["size"] = static_cast<Json::UInt64>(size);
                json["mime_type"] = mime_type;
                json["hash"] = hash;
            } else {
                json["item_count"] = item_count;
            }

            json["created_at"] = created_at;
            json["updated_at"] = updated_at;
            return json;
        }
    };

    /**
     * @brief 获取文件列表响应 DTO
     *
     * @details
     * 包含文件列表项和分页信息。
     */
    struct FileListResponse {
        std::vector<FileListItem> items;
        Pagination pagination;

        /// 转换为 JSON
        [[nodiscard]]
        auto ToJson() const -> Json::Value {
            Json::Value json;
            Json::Value items_array(Json::arrayValue);
            for (const auto& item : items) {
                items_array.append(item.ToJson());
            }
            json["items"] = items_array;
            json["pagination"] = pagination.ToJson();
            return json;
        }
    };

    // ==================== Download Info ====================

    /**
     * @brief 获取下载信息请求 DTO（路径参数）
     *
     * @details
     * 验证规则：
     * - file_id: 正整数
     *
     * 从 URL 路径参数解析。
     */
    struct DownloadInfoRequest {
        uint64_t file_id{ 0 };

        /// 从路径参数解析并验证，返回 Result
        [[nodiscard]]
        static auto FromPath(const std::string& file_id_str) -> Result<DownloadInfoRequest> {
            LOG_DEBUG << "开始解析下载信息请求参数";

            if (file_id_str.empty()) {
                LOG_WARN << "缺少必需参数: file_id";
                return std::unexpected(
                    ErrorInfo(ErrorCode::InvalidParameter, "缺少必需参数: file_id")
                );
            }

            // 检查是否为负数（stoull 会将负数回绕）
            if (file_id_str[0] == '-') {
                LOG_WARN << "参数 'file_id' 必须是正整数: " << file_id_str;
                return std::unexpected(
                    ErrorInfo(ErrorCode::InvalidParameter, "参数 'file_id' 必须是正整数")
                );
            }

            uint64_t file_id = 0;
            try {
                size_t pos = 0;
                file_id = std::stoull(file_id_str, &pos);
                if (pos != file_id_str.length() || file_id == 0) {
                    LOG_WARN << "参数 'file_id' 必须是正整数: " << file_id_str;
                    return std::unexpected(
                        ErrorInfo(ErrorCode::InvalidParameter, "参数 'file_id' 必须是正整数")
                    );
                }
            } catch (const std::exception& e) {
                LOG_WARN << "参数 'file_id' 格式无效: " << file_id_str;
                return std::unexpected(
                    ErrorInfo(ErrorCode::InvalidParameter, "参数 'file_id' 格式无效")
                );
            }

            DownloadInfoRequest request;
            request.file_id = file_id;

            LOG_DEBUG << "解析到下载信息请求: file_id=" << request.file_id;

            return request;
        }
    };

    /**
     * @brief 获取下载信息响应 DTO
     *
     * @details
     * 包含文件下载所需的元数据。
     */
    struct DownloadInfoResponse {
        uint64_t file_id{ 0 };
        std::string filename;
        uint64_t file_size{ 0 };
        std::string file_hash; ///< MD5 hash
        std::string mime_type;
        bool supports_range{ true };

        /// 转换为 JSON
        [[nodiscard]]
        auto ToJson() const -> Json::Value {
            Json::Value json;
            json["file_id"] = static_cast<Json::UInt64>(file_id);
            json["filename"] = filename;
            json["file_size"] = static_cast<Json::UInt64>(file_size);
            json["file_hash"] = file_hash;
            json["mime_type"] = mime_type;
            json["supports_range"] = supports_range;
            return json;
        }
    };

    // ==================== Download ====================

    /**
     * @brief 下载文件请求 DTO（路径参数）
     *
     * @details
     * 验证规则：
     * - file_id: 正整数
     *
     * 从 URL 路径参数解析。
     * Range 请求头通过 RangeRequest 结构体解析（参考 ShareDto.hpp 模式）。
     */
    struct DownloadRequest {
        uint64_t file_id{ 0 };

        /// 从路径参数解析并验证，返回 Result
        [[nodiscard]]
        static auto FromPath(const std::string& file_id_str) -> Result<DownloadRequest> {
            LOG_DEBUG << "开始解析下载文件请求参数";

            if (file_id_str.empty()) {
                LOG_WARN << "缺少必需参数: file_id";
                return std::unexpected(
                    ErrorInfo(ErrorCode::InvalidParameter, "缺少必需参数: file_id")
                );
            }

            // 检查是否为负数（stoull 会将负数回绕）
            if (file_id_str[0] == '-') {
                LOG_WARN << "参数 'file_id' 必须是正整数: " << file_id_str;
                return std::unexpected(
                    ErrorInfo(ErrorCode::InvalidParameter, "参数 'file_id' 必须是正整数")
                );
            }

            uint64_t file_id = 0;
            try {
                size_t pos = 0;
                file_id = std::stoull(file_id_str, &pos);
                if (pos != file_id_str.length() || file_id == 0) {
                    LOG_WARN << "参数 'file_id' 必须是正整数: " << file_id_str;
                    return std::unexpected(
                        ErrorInfo(ErrorCode::InvalidParameter, "参数 'file_id' 必须是正整数")
                    );
                }
            } catch (const std::exception& e) {
                LOG_WARN << "参数 'file_id' 格式无效: " << file_id_str;
                return std::unexpected(
                    ErrorInfo(ErrorCode::InvalidParameter, "参数 'file_id' 格式无效")
                );
            }

            DownloadRequest request;
            request.file_id = file_id;

            LOG_DEBUG << "解析到下载文件请求: file_id=" << request.file_id;

            return request;
        }
    };

    /**
     * @brief Range 请求解析结果
     *
     * @details
     * 用于解析 HTTP Range 请求头，支持断点续传和分片下载。
     */
    struct RangeRequest {
        bool has_range{ false };
        uint64_t start{ 0 };
        uint64_t end{ 0 };
        bool satisfiable{ true };
        uint64_t file_size{ 0 };

        /// 解析 Range 请求头
        [[nodiscard]]
        static auto Parse(const std::string& range_header, uint64_t file_size) -> RangeRequest {
            RangeRequest result;
            result.file_size = file_size;

            if (range_header.empty()) {
                result.has_range = false;
                return result;
            }

            result.has_range = true;

            std::string range = range_header;
            if (range.substr(0, 6) == "bytes=") {
                range = range.substr(6);
            } else {
                result.satisfiable = false;
                return result;
            }

            size_t dash_pos = range.find('-');
            if (dash_pos == std::string::npos) {
                result.satisfiable = false;
                return result;
            }

            std::string start_str = range.substr(0, dash_pos);
            std::string end_str = range.substr(dash_pos + 1);

            try {
                if (start_str.empty()) {
                    if (end_str.empty()) {
                        result.satisfiable = false;
                        return result;
                    }
                    uint64_t suffix_length = std::stoull(end_str);
                    if (suffix_length == 0 || suffix_length > file_size) {
                        result.start = 0;
                        result.end = file_size - 1;
                    } else {
                        result.start = file_size - suffix_length;
                        result.end = file_size - 1;
                    }
                } else {
                    result.start = std::stoull(start_str);
                    if (result.start >= file_size) {
                        result.satisfiable = false;
                        return result;
                    }
                    if (end_str.empty()) {
                        result.end = file_size - 1;
                    } else {
                        result.end = std::stoull(end_str);
                        if (result.end >= file_size) {
                            result.end = file_size - 1;
                        }
                    }
                }

                if (result.start > result.end) {
                    result.satisfiable = false;
                }
            } catch (...) {
                result.satisfiable = false;
            }

            return result;
        }
    };

    // ==================== Rename ====================

    /**
     * @brief 重命名请求 DTO
     *
     * @details
     * 验证规则：
     * - file_id: 正整数（路径参数）
     * - new_name: 1-255字符，禁止字符 / \ : * ? " < > | 及控制字符
     * - new_name: 禁止保留名称 "." 和 ".."
     * - new_name: 禁止以 "." 开头（隐藏文件）
     * - new_name: 仅允许 ASCII 可打印字符 (0x20-0x7E)
     *
     * 从 URL 路径参数和请求体解析。
     */
    struct RenameRequest {
        uint64_t file_id{ 0 };
        std::string new_name;

        /// 从路径参数和请求体解析并验证，返回 Result
        [[nodiscard]]
        static auto
        FromPathAndRequest(const std::string& file_id_str, const drogon::HttpRequestPtr& req)
            -> Result<RenameRequest> {
            LOG_DEBUG << "开始解析重命名请求参数";

            // 验证路径参数 file_id
            if (file_id_str.empty()) {
                LOG_WARN << "缺少必需参数: file_id";
                return std::unexpected(
                    ErrorInfo(ErrorCode::InvalidParameter, "缺少必需参数: file_id")
                );
            }

            // 检查是否为负数
            if (file_id_str[0] == '-') {
                LOG_WARN << "参数 'file_id' 必须是正整数: " << file_id_str;
                return std::unexpected(
                    ErrorInfo(ErrorCode::InvalidParameter, "参数 'file_id' 必须是正整数")
                );
            }

            uint64_t file_id = 0;
            try {
                size_t pos = 0;
                file_id = std::stoull(file_id_str, &pos);
                if (pos != file_id_str.length() || file_id == 0) {
                    LOG_WARN << "参数 'file_id' 必须是正整数: " << file_id_str;
                    return std::unexpected(
                        ErrorInfo(ErrorCode::InvalidParameter, "参数 'file_id' 必须是正整数")
                    );
                }
            } catch (const std::exception& e) {
                LOG_WARN << "参数 'file_id' 格式无效: " << file_id_str;
                return std::unexpected(
                    ErrorInfo(ErrorCode::InvalidParameter, "参数 'file_id' 格式无效")
                );
            }

            auto json_ptr = req->getJsonObject();
            if (!json_ptr) {
                LOG_WARN << "请求体不是有效的 JSON";
                return std::unexpected(
                    ErrorInfo(ErrorCode::ValidationFailed, "请求体不是有效的 JSON")
                );
            }

            const auto& json = *json_ptr;

            // 检查必填字段 new_name
            if (!json.isMember("new_name")) {
                LOG_WARN << "缺少必需参数: new_name";
                return std::unexpected(
                    ErrorInfo(ErrorCode::ValidationFailed, "缺少必需参数: new_name")
                );
            }

            if (!json["new_name"].isString()) {
                LOG_WARN << "参数 'new_name' 类型错误: 期望字符串";
                return std::unexpected(
                    ErrorInfo(ErrorCode::ValidationFailed, "参数 'new_name' 类型错误: 期望字符串")
                );
            }

            RenameRequest request;
            request.file_id = file_id;
            request.new_name = json["new_name"].asString();

            LOG_DEBUG << "解析到重命名请求: file_id=" << request.file_id << ", new_name=\""
                      << request.new_name << "\"";

            // 验证新文件名
            if (!request.ValidateFilenameLength()) {
                LOG_WARN << "文件名长度无效: " << request.new_name.length();
                return std::unexpected(
                    ErrorInfo(ErrorCode::ValidationFailed, "文件名长度必须在 1-255 字符之间")
                );
            }

            if (!request.ValidateFilenameForbiddenChars()) {
                LOG_WARN << "文件名包含禁止字符: " << request.new_name;
                return std::unexpected(ErrorInfo(
                    ErrorCode::InvalidFilename,
                    "文件名包含禁止字符：/ \\ : * ? \" < > | 或控制字符"
                ));
            }

            if (!request.ValidateFilenameReservedNames()) {
                LOG_WARN << "文件名为保留名称: " << request.new_name;
                return std::unexpected(
                    ErrorInfo(ErrorCode::InvalidFilename, "文件名不能为保留名称 \".\" 或 \"..\"")
                );
            }

            if (!request.ValidateFilenameNotHidden()) {
                LOG_WARN << "文件名以点开头（隐藏文件）: " << request.new_name;
                return std::unexpected(
                    ErrorInfo(ErrorCode::InvalidFilename, "文件名不能以 \".\" 开头")
                );
            }

            if (!request.ValidateFilenameCharset()) {
                LOG_WARN << "文件名包含非 ASCII 字符: " << request.new_name;
                return std::unexpected(
                    ErrorInfo(ErrorCode::InvalidFilename, "文件名仅允许 ASCII 可打印字符")
                );
            }

            LOG_DEBUG << "请求参数验证通过";
            return request;
        }

    private:
        /// 验证文件名长度 (1-255 字符)
        [[nodiscard]]
        auto ValidateFilenameLength() const -> bool {
            return new_name.length() >= 1 && new_name.length() <= 255;
        }

        /// 验证文件名禁止字符 (/ \ : * ? " < > | 及控制字符 0x00-0x1F)
        [[nodiscard]]
        auto ValidateFilenameForbiddenChars() const -> bool {
            static const char forbidden_chars[] = "/\\:*?\"<>|";
            for (char c : new_name) {
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
            return new_name != "." && new_name != "..";
        }

        /// 验证文件名不以点开头（非隐藏文件）
        [[nodiscard]]
        auto ValidateFilenameNotHidden() const -> bool {
            return new_name.empty() || new_name[0] != '.';
        }

        /// 验证文件名字符集（仅 ASCII 可打印字符 0x20-0x7E）
        [[nodiscard]]
        auto ValidateFilenameCharset() const -> bool {
            for (char c : new_name) {
                // ASCII 可打印字符范围: 0x20 (空格) 到 0x7E (~)
                if (static_cast<unsigned char>(c) < 0x20 || static_cast<unsigned char>(c) > 0x7E) {
                    return false;
                }
            }
            return true;
        }
    };

    /**
     * @brief 重命名响应 DTO
     *
     * @details
     * 包含重命名后的文件信息。
     */
    struct RenameResponse {
        uint64_t id{ 0 };
        std::string name;
        std::string updated_at;

        /// 转换为 JSON
        [[nodiscard]]
        auto ToJson() const -> Json::Value {
            Json::Value json;
            json["id"] = static_cast<Json::UInt64>(id);
            json["name"] = name;
            json["updated_at"] = updated_at;
            return json;
        }
    };

    // ==================== Move ====================

    /**
     * @brief 移动文件请求 DTO
     *
     * @details
     * 验证规则：
     * - file_ids: 非空数组，每个元素为正整数
     * - target_folder_id: 默认 0（根目录）
     *
     * 从请求体解析。
     */
    struct MoveRequest {
        std::vector<uint64_t> file_ids;
        uint64_t target_folder_id{ 0 };

        /// 从 HTTP 请求解析并验证，返回 Result
        [[nodiscard]]
        static auto FromRequest(const drogon::HttpRequestPtr& req) -> Result<MoveRequest> {
            LOG_DEBUG << "开始解析移动文件请求参数";

            auto json_ptr = req->getJsonObject();
            if (!json_ptr) {
                LOG_WARN << "请求体不是有效的 JSON";
                return std::unexpected(
                    ErrorInfo(ErrorCode::ValidationFailed, "请求体不是有效的 JSON")
                );
            }

            const auto& json = *json_ptr;

            // 检查必填字段 file_ids
            if (!json.isMember("file_ids")) {
                LOG_WARN << "缺少必需参数: file_ids";
                return std::unexpected(
                    ErrorInfo(ErrorCode::ValidationFailed, "缺少必需参数: file_ids")
                );
            }

            if (!json["file_ids"].isArray()) {
                LOG_WARN << "参数 'file_ids' 类型错误: 期望数组";
                return std::unexpected(
                    ErrorInfo(ErrorCode::InvalidParameter, "参数 'file_ids' 类型错误: 期望数组")
                );
            }

            MoveRequest request;

            // 解析 file_ids
            const auto& file_ids_array = json["file_ids"];
            if (file_ids_array.empty()) {
                LOG_WARN << "参数 'file_ids' 不能为空数组";
                return std::unexpected(
                    ErrorInfo(ErrorCode::InvalidParameter, "参数 'file_ids' 不能为空数组")
                );
            }

            for (const auto& item : file_ids_array) {
                if (!item.isIntegral()) {
                    LOG_WARN << "参数 'file_ids' 中的元素类型错误: 期望整数";
                    return std::unexpected(ErrorInfo(
                        ErrorCode::InvalidParameter,
                        "参数 'file_ids' 中的元素类型错误: 期望整数"
                    ));
                }
                auto file_id = item.asUInt64();
                if (file_id == 0) {
                    LOG_WARN << "参数 'file_ids' 中的元素必须为正整数";
                    return std::unexpected(ErrorInfo(
                        ErrorCode::InvalidParameter,
                        "参数 'file_ids' 中的元素必须为正整数"
                    ));
                }
                request.file_ids.push_back(file_id);
            }

            // 解析可选参数 target_folder_id
            if (json.isMember("target_folder_id")) {
                if (!json["target_folder_id"].isIntegral()) {
                    LOG_WARN << "参数 'target_folder_id' 类型错误: 期望整数";
                    return std::unexpected(ErrorInfo(
                        ErrorCode::ValidationFailed,
                        "参数 'target_folder_id' 类型错误: 期望整数"
                    ));
                }
                request.target_folder_id = json["target_folder_id"].asUInt64();
            }

            LOG_DEBUG << "解析到移动文件请求: file_ids.size()=" << request.file_ids.size()
                      << ", target_folder_id=" << request.target_folder_id;

            return request;
        }
    };

    /**
     * @brief 移动文件响应 DTO
     *
     * @details
     * 包含移动操作的结果统计。
     */
    struct MoveResponse {
        int moved_count{ 0 };

        /// 转换为 JSON
        [[nodiscard]]
        auto ToJson() const -> Json::Value {
            Json::Value json;
            json["moved_count"] = moved_count;
            return json;
        }
    };

    // ==================== Copy ====================

    /**
     * @brief 文件ID映射
     *
     * @details
     * 用于复制操作中记录旧ID和新ID的映射关系。
     */
    struct FileIdMapping {
        uint64_t old_id{ 0 };
        uint64_t new_id{ 0 };

        /// 转换为 JSON
        [[nodiscard]]
        auto ToJson() const -> Json::Value {
            Json::Value json;
            json["old_id"] = static_cast<Json::UInt64>(old_id);
            json["new_id"] = static_cast<Json::UInt64>(new_id);
            return json;
        }
    };

    /**
     * @brief 复制文件请求 DTO
     *
     * @details
     * 验证规则：
     * - file_ids: 非空数组，每个元素为正整数
     * - target_folder_id: 默认 0（根目录）
     *
     * 从请求体解析。
     */
    struct CopyRequest {
        std::vector<uint64_t> file_ids;
        uint64_t target_folder_id{ 0 };

        /// 从 HTTP 请求解析并验证，返回 Result
        [[nodiscard]]
        static auto FromRequest(const drogon::HttpRequestPtr& req) -> Result<CopyRequest> {
            LOG_DEBUG << "开始解析复制文件请求参数";

            auto json_ptr = req->getJsonObject();
            if (!json_ptr) {
                LOG_WARN << "请求体不是有效的 JSON";
                return std::unexpected(
                    ErrorInfo(ErrorCode::ValidationFailed, "请求体不是有效的 JSON")
                );
            }

            const auto& json = *json_ptr;

            // 检查必填字段 file_ids
            if (!json.isMember("file_ids")) {
                LOG_WARN << "缺少必需参数: file_ids";
                return std::unexpected(
                    ErrorInfo(ErrorCode::ValidationFailed, "缺少必需参数: file_ids")
                );
            }

            if (!json["file_ids"].isArray()) {
                LOG_WARN << "参数 'file_ids' 类型错误: 期望数组";
                return std::unexpected(
                    ErrorInfo(ErrorCode::InvalidParameter, "参数 'file_ids' 类型错误: 期望数组")
                );
            }

            CopyRequest request;

            // 解析 file_ids
            const auto& file_ids_array = json["file_ids"];
            if (file_ids_array.empty()) {
                LOG_WARN << "参数 'file_ids' 不能为空数组";
                return std::unexpected(
                    ErrorInfo(ErrorCode::InvalidParameter, "参数 'file_ids' 不能为空数组")
                );
            }

            for (const auto& item : file_ids_array) {
                if (!item.isIntegral()) {
                    LOG_WARN << "参数 'file_ids' 中的元素类型错误: 期望整数";
                    return std::unexpected(ErrorInfo(
                        ErrorCode::InvalidParameter,
                        "参数 'file_ids' 中的元素类型错误: 期望整数"
                    ));
                }
                auto file_id = item.asUInt64();
                if (file_id == 0) {
                    LOG_WARN << "参数 'file_ids' 中的元素必须为正整数";
                    return std::unexpected(ErrorInfo(
                        ErrorCode::InvalidParameter,
                        "参数 'file_ids' 中的元素必须为正整数"
                    ));
                }
                request.file_ids.push_back(file_id);
            }

            // 解析可选参数 target_folder_id
            if (json.isMember("target_folder_id")) {
                if (!json["target_folder_id"].isIntegral()) {
                    LOG_WARN << "参数 'target_folder_id' 类型错误: 期望整数";
                    return std::unexpected(ErrorInfo(
                        ErrorCode::ValidationFailed,
                        "参数 'target_folder_id' 类型错误: 期望整数"
                    ));
                }
                request.target_folder_id = json["target_folder_id"].asUInt64();
            }

            LOG_DEBUG << "解析到复制文件请求: file_ids.size()=" << request.file_ids.size()
                      << ", target_folder_id=" << request.target_folder_id;

            return request;
        }
    };

    /**
     * @brief 复制文件响应 DTO
     *
     * @details
     * 包含复制操作的结果统计和ID映射。
     */
    struct CopyResponse {
        int copied_count{ 0 };
        std::vector<FileIdMapping> new_files;

        /// 转换为 JSON
        [[nodiscard]]
        auto ToJson() const -> Json::Value {
            Json::Value json;
            json["copied_count"] = copied_count;

            Json::Value files_array(Json::arrayValue);
            for (const auto& mapping : new_files) {
                files_array.append(mapping.ToJson());
            }
            json["new_files"] = files_array;

            return json;
        }
    };

    // ==================== Delete ====================

    /**
     * @brief 删除文件请求 DTO
     *
     * @details
     * 验证规则：
     * - file_ids: 非空数组，每个元素为正整数
     *
     * 从请求体解析。
     * 注意：删除操作为软删除，文件移入回收站。
     */
    struct DeleteRequest {
        std::vector<uint64_t> file_ids;

        /// 从 HTTP 请求解析并验证，返回 Result
        [[nodiscard]]
        static auto FromRequest(const drogon::HttpRequestPtr& req) -> Result<DeleteRequest> {
            LOG_DEBUG << "开始解析删除文件请求参数";

            auto json_ptr = req->getJsonObject();
            if (!json_ptr) {
                LOG_WARN << "请求体不是有效的 JSON";
                return std::unexpected(
                    ErrorInfo(ErrorCode::ValidationFailed, "请求体不是有效的 JSON")
                );
            }

            const auto& json = *json_ptr;

            // 检查必填字段 file_ids
            if (!json.isMember("file_ids")) {
                LOG_WARN << "缺少必需参数: file_ids";
                return std::unexpected(
                    ErrorInfo(ErrorCode::ValidationFailed, "缺少必需参数: file_ids")
                );
            }

            if (!json["file_ids"].isArray()) {
                LOG_WARN << "参数 'file_ids' 类型错误: 期望数组";
                return std::unexpected(
                    ErrorInfo(ErrorCode::InvalidParameter, "参数 'file_ids' 类型错误: 期望数组")
                );
            }

            DeleteRequest request;

            // 解析 file_ids
            const auto& file_ids_array = json["file_ids"];
            if (file_ids_array.empty()) {
                LOG_WARN << "参数 'file_ids' 不能为空数组";
                return std::unexpected(
                    ErrorInfo(ErrorCode::InvalidParameter, "参数 'file_ids' 不能为空数组")
                );
            }

            for (const auto& item : file_ids_array) {
                if (!item.isIntegral()) {
                    LOG_WARN << "参数 'file_ids' 中的元素类型错误: 期望整数";
                    return std::unexpected(ErrorInfo(
                        ErrorCode::InvalidParameter,
                        "参数 'file_ids' 中的元素类型错误: 期望整数"
                    ));
                }
                auto file_id = item.asUInt64();
                if (file_id == 0) {
                    LOG_WARN << "参数 'file_ids' 中的元素必须为正整数";
                    return std::unexpected(ErrorInfo(
                        ErrorCode::InvalidParameter,
                        "参数 'file_ids' 中的元素必须为正整数"
                    ));
                }
                request.file_ids.push_back(file_id);
            }

            LOG_DEBUG << "解析到删除文件请求: file_ids.size()=" << request.file_ids.size();

            return request;
        }
    };

    /**
     * @brief 删除文件响应 DTO
     *
     * @details
     * 包含删除操作的结果统计。
     */
    struct DeleteResponse {
        int deleted_count{ 0 };

        /// 转换为 JSON
        [[nodiscard]]
        auto ToJson() const -> Json::Value {
            Json::Value json;
            json["deleted_count"] = deleted_count;
            return json;
        }
    };

    // ==================== DownloadInfo (Internal Service Use) ====================

    /**
     * @brief 下载信息结构（内部服务使用）
     *
     * @details
     * 用于服务层返回下载所需的所有信息，包括文件存储路径。
     * 控制器使用此结构构造 HTTP 响应。
     */
    struct DownloadInfo {
        uint64_t file_id{ 0 };
        std::string filename;
        uint64_t file_size{ 0 };
        std::string file_hash;    ///< MD5 hash
        std::string mime_type;
        std::string storage_path; ///< 文件物理存储路径
        bool supports_range{ true };
    };

    // ==================== Search ====================

    /**
     * @brief 文件搜索请求 DTO
     *
     * @details
     * 验证规则：
     * - keyword: 必填，1-100字符
     * - type: 默认 all，可选值 all/file/folder
     * - folder_id: 默认空（全局搜索），指定时限定搜索范围
     * - page: 默认 1，必须 > 0
     * - page_size: 默认 20，必须 > 0 且 <= 100
     *
     * 从 URL 查询参数解析。
     */
    struct SearchRequest {
        std::string keyword;
        std::string type{ "all" };
        std::optional<uint64_t> folder_id;
        int page{ 1 };
        int page_size{ 20 };

        /// 从 HTTP 请求查询参数解析并验证，返回 Result
        [[nodiscard]]
        static auto FromRequest(const drogon::HttpRequestPtr& req) -> Result<SearchRequest> {
            LOG_DEBUG << "开始解析文件搜索请求参数";

            SearchRequest request;

            // 有效类型
            static const std::set<std::string> valid_types = { "all", "file", "folder" };

            // 解析必填参数 keyword
            auto keyword_str = req->getParameter("keyword");
            if (keyword_str.empty()) {
                LOG_WARN << "缺少必需参数: keyword";
                return std::unexpected(
                    ErrorInfo(ErrorCode::ValidationFailed, "缺少必需参数: keyword")
                );
            }

            // 验证 keyword 长度
            if (keyword_str.length() < 1 || keyword_str.length() > 100) {
                LOG_WARN << "参数 'keyword' 长度无效: " << keyword_str.length();
                return std::unexpected(ErrorInfo(
                    ErrorCode::ValidationFailed,
                    "参数 'keyword' 长度必须在 1-100 字符之间"
                ));
            }

            // 过滤 keyword 中的特殊字符（防止 SQL 注入）
            for (char c : keyword_str) {
                if (c == '%' || c == '_' || c == '\\' || c == '\'' || c == '"') {
                    LOG_WARN << "参数 'keyword' 包含禁止字符: " << c;
                    return std::unexpected(
                        ErrorInfo(ErrorCode::ValidationFailed, "参数 'keyword' 包含禁止字符")
                    );
                }
            }

            request.keyword = keyword_str;

            // 解析可选参数 type
            auto type_str = req->getParameter("type");
            if (!type_str.empty()) {
                if (valid_types.find(type_str) == valid_types.end()) {
                    LOG_WARN << "参数 'type' 值无效: " << type_str;
                    return std::unexpected(ErrorInfo(
                        ErrorCode::ValidationFailed,
                        "参数 'type' 值无效，必须是 all/file/folder"
                    ));
                }
                request.type = type_str;
            }

            // 解析可选参数 folder_id
            auto folder_id_str = req->getParameter("folder_id");
            if (!folder_id_str.empty()) {
                try {
                    size_t pos = 0;
                    auto value = std::stoull(folder_id_str, &pos);
                    if (pos != folder_id_str.length()) {
                        LOG_WARN << "参数 'folder_id' 格式无效: " << folder_id_str;
                        return std::unexpected(
                            ErrorInfo(ErrorCode::ValidationFailed, "参数 'folder_id' 格式无效")
                        );
                    }
                    request.folder_id = value;
                } catch (const std::exception& e) {
                    LOG_WARN << "参数 'folder_id' 格式无效: " << folder_id_str;
                    return std::unexpected(
                        ErrorInfo(ErrorCode::ValidationFailed, "参数 'folder_id' 格式无效")
                    );
                }
            }

            // 解析可选参数 page
            auto page_str = req->getParameter("page");
            if (!page_str.empty()) {
                try {
                    size_t pos = 0;
                    auto value = std::stoi(page_str, &pos);
                    if (pos != page_str.length() || value <= 0) {
                        LOG_WARN << "参数 'page' 格式无效或值无效: " << page_str;
                        return std::unexpected(
                            ErrorInfo(ErrorCode::ValidationFailed, "参数 'page' 必须是正整数")
                        );
                    }
                    request.page = value;
                } catch (const std::exception& e) {
                    LOG_WARN << "参数 'page' 格式无效: " << page_str;
                    return std::unexpected(
                        ErrorInfo(ErrorCode::ValidationFailed, "参数 'page' 格式无效")
                    );
                }
            }

            // 解析可选参数 page_size
            auto page_size_str = req->getParameter("page_size");
            if (!page_size_str.empty()) {
                try {
                    size_t pos = 0;
                    auto value = std::stoi(page_size_str, &pos);
                    if (pos != page_size_str.length() || value <= 0 || value > 100) {
                        LOG_WARN << "参数 'page_size' 格式无效或值无效: " << page_size_str;
                        return std::unexpected(ErrorInfo(
                            ErrorCode::ValidationFailed,
                            "参数 'page_size' 必须是 1-100 之间的整数"
                        ));
                    }
                    request.page_size = value;
                } catch (const std::exception& e) {
                    LOG_WARN << "参数 'page_size' 格式无效: " << page_size_str;
                    return std::unexpected(
                        ErrorInfo(ErrorCode::ValidationFailed, "参数 'page_size' 格式无效")
                    );
                }
            }

            LOG_DEBUG << "解析到文件搜索请求: keyword=\"" << request.keyword
                      << "\", type=" << request.type << ", folder_id="
                      << (request.folder_id.has_value() ? std::to_string(*request.folder_id) :
                                                          "null")
                      << ", page=" << request.page << ", page_size=" << request.page_size;

            return request;
        }
    };

    /**
     * @brief 搜索结果项
     *
     * @details
     * 继承 FileListItem，添加路径信息用于搜索结果展示。
     */
    struct SearchResultItem : FileListItem {
        std::string path; ///< 文件路径（面包屑）

        /// 转换为 JSON
        [[nodiscard]]
        auto ToJson() const -> Json::Value {
            auto json = FileListItem::ToJson();
            json["path"] = path;
            return json;
        }
    };

    /**
     * @brief 文件搜索响应 DTO
     *
     * @details
     * 包含搜索结果项和分页信息。
     */
    struct SearchResponse {
        std::vector<SearchResultItem> items;
        Pagination pagination;

        /// 转换为 JSON
        [[nodiscard]]
        auto ToJson() const -> Json::Value {
            Json::Value json;
            Json::Value items_array(Json::arrayValue);
            for (const auto& item : items) {
                items_array.append(item.ToJson());
            }
            json["items"] = items_array;
            json["pagination"] = pagination.ToJson();
            return json;
        }
    };

} // namespace disk::file
