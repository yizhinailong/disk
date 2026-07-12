/**
 * @file FileDto.hpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 文件模块数据传输对象
 *
 * @copyright Copyright (c) 2026
 *
 * @details
 * 本文件包含文件模块的所有数据传输对象（DTO）：
 * - FileItem: 文件项（共享响应组件）
 * - InitUploadRequest: 初始化上传请求
 * - InitUploadResponse: 初始化上传响应
 * - UploadChunkResponse: 上传分片响应
 * - CompleteUploadRequest: 完成上传请求
 * - CompleteUploadResponse: 完成上传响应
 * - FileListRequest: 获取文件列表请求
 * - FileListItem: 文件列表项
 * - FileListResponse: 获取文件列表响应
 * - DownloadInfoRequest: 获取下载信息请求
 * - DownloadInfoResponse: 获取下载信息响应
 * - DownloadRequest: 下载文件请求
 * - RangeRequest: Range 请求解析结果
 * - RenameRequest: 重命名请求
 * - RenameResponse: 重命名响应
 * - MoveRequest: 移动文件请求
 * - MoveResponse: 移动文件响应
 * - CopyRequest: 复制文件请求
 * - CopyResponse: 复制文件响应
 * - FileIdMapping: 文件ID映射
 * - DeleteRequest: 删除文件请求
 * - DeleteResponse: 删除文件响应
 * - DownloadInfo: 下载信息结构（内部服务使用）
 * - SearchRequest: 文件搜索请求
 * - SearchResultItem: 搜索结果项
 * - SearchResponse: 文件搜索响应
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

#include "utils/DtoBase.hpp"
#include "utils/ErrorCode.hpp"
#include "utils/NameValidation.hpp"
#include "utils/Response.hpp"

namespace disk::file {

    /// ==================== FileItem（共享响应组件）====================

    /**
     * @brief 文件项数据
     *
     * @details
     * 用于表示单个文件的基本信息。
     * 可在多种响应中复用（如列表、详情、上传完成等）。
     */
    struct FileItem : DtoBase<FileItem> {
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
            SetField(json, "id", id);
            SetField(json, "name", name);
            SetField(json, "size", size);
            SetField(json, "hash", hash);
            SetField(json, "mime_type", mime_type);
            SetField(json, "parent_id", parent_id);
            SetField(json, "created_at", created_at);
            return json;
        }
    };

    /// ==================== Init Upload ====================

    /**
     * @brief 初始化上传请求 DTO
     *
     * @details
     * 验证规则：
     * - filename: 1-255字符，禁止字符 / \ : * ? " < > | 及控制字符
     * - filename: 禁止保留名称 "." 和 ".."
     * - filename: 禁止以 "." 开头（隐藏文件）
     * - filename: 必须是合法 UTF-8，禁止控制字符
     * - file_size: 必须 > 0（最大文件大小由服务层验证）
     * - file_hash: 32字符的十六进制字符串（MD5）
     * - parent_id: 默认 0（根目录）
     */
    struct InitUploadRequest : DtoBase<InitUploadRequest> {
        std::string filename;
        uint64_t file_size{ 0 };
        std::string file_hash;
        uint64_t parent_id{ 0 };

        /// 从 HTTP 请求解析并验证，返回 Result
        [[nodiscard]]
        static auto FromRequest(const drogon::HttpRequestPtr& req) -> Result<InitUploadRequest> {
            Logger::Debug() << "Start parsing init upload request parameters";

            auto json_result = RequireJsonBody(req);
            if (!json_result) return std::unexpected(json_result.error());
            const auto& json = *json_result.value();

            auto filename_result = RequireString(json, "filename");
            if (!filename_result) return std::unexpected(filename_result.error());

            auto file_size_result = RequireUInt64(json, "file_size");
            if (!file_size_result) return std::unexpected(file_size_result.error());

            auto file_hash_result = RequireString(json, "file_hash");
            if (!file_hash_result) return std::unexpected(file_hash_result.error());

            auto parent_id_result = OptionalUInt64(json, "parent_id");
            if (!parent_id_result) return std::unexpected(parent_id_result.error());

            InitUploadRequest request;
            request.filename = std::move(*filename_result);
            request.file_size = *file_size_result;
            request.file_hash = std::move(*file_hash_result);
            if (parent_id_result->has_value()) {
                request.parent_id = **parent_id_result;
            }

            Logger::Debug() << "Parsed init upload request: filename=\"" << request.filename
                      << "\", file_size=" << request.file_size
                      << ", file_hash=" << request.file_hash << ", parent_id=" << request.parent_id;

            /// 验证文件名
            if (!request.ValidateFilenameLength()) {
                Logger::Warn() << "Invalid filename length: " << request.filename.length();
                return std::unexpected(ErrorInfo(
                    ErrorCode::ValidationFailed,
                    "Filename length must be between 1-255 characters"
                ));
            }

            if (!request.ValidateFilenameForbiddenChars()) {
                Logger::Warn() << "Filename contains forbidden characters: " << request.filename;
                return std::unexpected(ErrorInfo(
                    ErrorCode::InvalidFilename,
                    "Filename contains forbidden characters: / \\ : * ? \" < > | or " "control " "c" "h" "a" "r" "a" "c" "t" "e" "r" "s"
                ));
            }

            if (!request.ValidateFilenameReservedNames()) {
                Logger::Warn() << "Filename is a reserved name: " << request.filename;
                return std::unexpected(ErrorInfo(
                    ErrorCode::InvalidFilename,
                    "Filename cannot be reserved name \".\" or \"..\""
                ));
            }

            if (!request.ValidateFilenameNotHidden()) {
                Logger::Warn() << "Filename starts with a dot (hidden file): " << request.filename;
                return std::unexpected(
                    ErrorInfo(ErrorCode::InvalidFilename, "Filename cannot start with \".\"")
                );
            }

            if (!request.ValidateFilenameCharset()) {
                Logger::Warn() << "Filename contains invalid UTF-8 or control characters: "
                         << request.filename;
                return std::unexpected(ErrorInfo(
                    ErrorCode::InvalidFilename,
                    "Filename must be valid UTF-8 and cannot contain control characters"
                ));
            }

            /// 验证文件大小
            if (!request.ValidateFileSize()) {
                Logger::Warn() << "Invalid file size: " << request.file_size;
                return std::unexpected(ErrorInfo(
                    ErrorCode::ValidationFailed,
                    "File size must be greater than 0 and not exceed the maximum limit"
                ));
            }

            /// 验证文件哈希
            if (!request.ValidateFileHash()) {
                Logger::Warn() << "Invalid file hash format: " << request.file_hash;
                return std::unexpected(ErrorInfo(
                    ErrorCode::ValidationFailed,
                    "File hash must be a 32-character lowercase hexadecimal string"
                ));
            }

            Logger::Debug() << "Request parameters validation passed";
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
                /// 检查控制字符 (0x00-0x1F)
                if (static_cast<unsigned char>(c) <= 0x1F) {
                    return false;
                }
                /// 检查文件系统保留字符
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

        /// 验证文件名字符集（合法 UTF-8，且不含控制字符）
        [[nodiscard]]
        auto ValidateFilenameCharset() const -> bool {
            return utils::IsValidUtf8WithoutControlChars(filename);
        }

        /// 验证文件大小 (> 0)
        /// 注意：最大文件大小验证由服务层完成，DTO 层仅验证非零
        [[nodiscard]]
        auto ValidateFileSize() const -> bool {
            return file_size > 0;
        }

        /// 验证文件哈希（32字符小写十六进制 MD5）
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
    struct InitUploadResponse : DtoBase<InitUploadResponse> {
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
            SetField(json, "upload_id", upload_id);
            SetField(json, "chunk_size", chunk_size);
            SetField(json, "total_chunks", total_chunks);
            SetArray(json, "uploaded_chunks", uploaded_chunks);
            SetField(json, "instant_upload", instant_upload);
            SetOptional(json, "file", file);
            return json;
        }
    };

    /// ==================== Upload Chunk ====================

    /**
     * @brief 上传分片响应 DTO
     *
     * @details
     * 包含分片上传结果。
     */
    struct UploadChunkResponse : DtoBase<UploadChunkResponse> {
        uint32_t chunk_index{ 0 }; ///< 分片索引
        bool uploaded{ false };    ///< 是否上传成功

        /// 转换为 JSON
        [[nodiscard]]
        auto ToJson() const -> Json::Value {
            Json::Value json;
            SetField(json, "chunk_index", chunk_index);
            SetField(json, "uploaded", uploaded);
            return json;
        }
    };

    /// ==================== Complete Upload ====================

    /**
     * @brief 完成上传请求 DTO
     *
     * @details
     * 验证规则：
     * - upload_id: 非空字符串
     */
    struct CompleteUploadRequest : DtoBase<CompleteUploadRequest> {
        std::string upload_id;

        /// 从 HTTP 请求解析并验证，返回 Result
        [[nodiscard]]
        static auto FromRequest(const drogon::HttpRequestPtr& req)
            -> Result<CompleteUploadRequest> {
            Logger::Debug() << "Start parsing complete upload request parameters";

            auto json_result = RequireJsonBody(req);
            if (!json_result) return std::unexpected(json_result.error());
            const auto& json = *json_result.value();

            auto upload_id_result = RequireString(json, "upload_id");
            if (!upload_id_result) return std::unexpected(upload_id_result.error());

            CompleteUploadRequest request;
            request.upload_id = std::move(*upload_id_result);

            Logger::Debug() << "Parsed complete upload request: upload_id=" << request.upload_id;

            /// 验证 upload_id 非空
            if (request.upload_id.empty()) {
                Logger::Warn() << "upload_id cannot be empty";
                return std::unexpected(
                    ErrorInfo(ErrorCode::ValidationFailed, "upload_id cannot be empty")
                );
            }

            Logger::Debug() << "Request parameters validation passed";
            return request;
        }
    };

    /**
     * @brief 完成上传响应 DTO
     *
     * @details
     * 包含上传完成后的文件信息。
     */
    struct CompleteUploadResponse : DtoBase<CompleteUploadResponse> {
        FileItem file; ///< 文件信息

        /// 转换为 JSON
        [[nodiscard]]
        auto ToJson() const -> Json::Value {
            Json::Value json;
            SetField(json, "file", file);
            return json;
        }
    };

    /// ==================== File List ====================

    /**
     * @brief 文件列表查询参数
     *
     * @details
     * 封装文件列表数据访问所需的分页、排序和过滤参数。
     */
    struct FileListQueryParams {
        uint64_t parent_id{ 0 };
        int page{ 1 };
        int page_size{ 20 };
        std::string sort_by{ "name" };
        std::string sort_order{ "asc" };
        std::string type{ "all" };

        [[nodiscard]]
        auto Offset() const -> int64_t {
            return static_cast<int64_t>(page - 1) * page_size;
        }
    };

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
    struct FileListRequest : DtoBase<FileListRequest> {
        FileListQueryParams query;

        /// 从 HTTP 请求查询参数解析并验证，返回 Result
        [[nodiscard]]
        static auto FromRequest(const drogon::HttpRequestPtr& req) -> Result<FileListRequest> {
            Logger::Debug() << "Start parsing file list request parameters";

            FileListRequest request;

            /// 有效排序字段
            static const std::set<std::string> valid_sort_by = { "name",
                                                                 "size",
                                                                 "created_at",
                                                                 "updated_at" };

            /// 有效排序方向
            static const std::set<std::string> valid_sort_order = { "asc", "desc" };

            /// 有效类型
            static const std::set<std::string> valid_types = { "all", "file", "folder" };

            /// 解析可选参数 parent_id
            auto parent_id_result = QueryUInt64(req, "parent_id");
            if (!parent_id_result) return std::unexpected(parent_id_result.error());
            if (parent_id_result->has_value()) {
                request.query.parent_id = **parent_id_result;
            }

            /// 解析可选参数 page
            auto page_result = QueryPositiveInt(req, "page", 1);
            if (!page_result) return std::unexpected(page_result.error());
            if (page_result->has_value()) {
                request.query.page = **page_result;
            }

            /// 解析可选参数 page_size
            auto page_size_result = QueryPositiveInt(req, "page_size", 1, 100);
            if (!page_size_result) return std::unexpected(page_size_result.error());
            if (page_size_result->has_value()) {
                request.query.page_size = **page_size_result;
            }

            /// 解析可选参数 sort_by
            auto sort_by_str = req->getParameter("sort_by");
            if (!sort_by_str.empty()) {
                if (valid_sort_by.find(sort_by_str) == valid_sort_by.end()) {
                    Logger::Warn() << "Parameter 'sort_by' invalid value: " << sort_by_str;
                    return std::unexpected(ErrorInfo(
                        ErrorCode::ValidationFailed,
                        "Parameter 'sort_by' invalid value, must be name/size/created_at/updated_at"
                    ));
                }
                request.query.sort_by = sort_by_str;
            }

            /// 解析可选参数 sort_order
            auto sort_order_str = req->getParameter("sort_order");
            if (!sort_order_str.empty()) {
                if (valid_sort_order.find(sort_order_str) == valid_sort_order.end()) {
                    Logger::Warn() << "Parameter 'sort_order' invalid value: " << sort_order_str;
                    return std::unexpected(ErrorInfo(
                        ErrorCode::ValidationFailed,
                        "Parameter 'sort_order' invalid value, must be asc/desc"
                    ));
                }
                request.query.sort_order = sort_order_str;
            }

            /// 解析可选参数 type
            auto type_str = req->getParameter("type");
            if (!type_str.empty()) {
                if (valid_types.find(type_str) == valid_types.end()) {
                    Logger::Warn() << "Parameter 'type' invalid value: " << type_str;
                    return std::unexpected(ErrorInfo(
                        ErrorCode::ValidationFailed,
                        "Parameter 'type' invalid value, must be all/file/folder"
                    ));
                }
                request.query.type = type_str;
            }

            Logger::Debug() << "Parsed file list request: parent_id=" << request.query.parent_id
                      << ", page=" << request.query.page << ", page_size=" << request.query.page_size
                      << ", sort_by=" << request.query.sort_by << ", sort_order=" << request.query.sort_order
                      << ", type=" << request.query.type;

            return request;
        }
    };

    /**
     * @brief 文件列表项
     *
     * @details
     * 用于表示文件列表中的单个文件或文件夹项。
     */
    struct FileListItem : DtoBase<FileListItem> {
        uint64_t id;
        std::string name;
        std::string type; ///< "file" 或 "folder"
        /// 对于文件：文件特有字段
        uint64_t size{ 0 };
        std::string mime_type;
        std::string hash;
        /// 对于文件夹：文件夹特有字段
        int item_count{ 0 };
        /// 公共：公共字段
        std::string created_at;
        std::string updated_at;

        /// 转换为 JSON
        [[nodiscard]]
        auto ToJson() const -> Json::Value {
            Json::Value json;
            SetField(json, "id", id);
            SetField(json, "name", name);
            SetField(json, "type", type);

            if (type == "file") {
                SetField(json, "size", size);
                SetField(json, "mime_type", mime_type);
                SetField(json, "hash", hash);
            } else {
                SetField(json, "item_count", item_count);
            }

            SetField(json, "created_at", created_at);
            SetField(json, "updated_at", updated_at);
            return json;
        }
    };

    /**
     * @brief 获取文件列表响应 DTO
     *
     * @details
     * 包含文件列表项和分页信息。
     */
    struct FileListResponse : DtoBase<FileListResponse> {
        std::vector<FileListItem> items;
        Pagination pagination;

        /// 转换为 JSON
        [[nodiscard]]
        auto ToJson() const -> Json::Value {
            Json::Value json;
            SetArray(json, "items", items);
            SetField(json, "pagination", pagination);
            return json;
        }

        /// 从 JSON 反序列化（用于 Redis 缓存读取）
        [[nodiscard]]
        static auto FromJson(const Json::Value& json) -> FileListResponse {
            FileListResponse response;

            /// 解析分页信息
            if (json.isMember("pagination") && json["pagination"].isObject()) {
                const auto& pag = json["pagination"];
                response.pagination.page = pag.get("page", 1).asInt();
                response.pagination.page_size = pag.get("page_size", 20).asInt();
                response.pagination.total = pag.get("total", 0).asInt();
                response.pagination.total_pages = pag.get("total_pages", 0).asInt();
            }

            /// 解析 items 数组
            if (json.isMember("items") && json["items"].isArray()) {
                for (const auto& item : json["items"]) {
                    FileListItem fli;
                    fli.id = item.get("id", 0).asUInt64();
                    fli.name = item.get("name", "").asString();
                    fli.type = item.get("type", "file").asString();
                    fli.size = item.get("size", 0).asUInt64();
                    fli.mime_type = item.get("mime_type", "").asString();
                    fli.hash = item.get("hash", "").asString();
                    fli.item_count = item.get("item_count", 0).asInt();
                    fli.created_at = item.get("created_at", "").asString();
                    fli.updated_at = item.get("updated_at", "").asString();
                    response.items.push_back(std::move(fli));
                }
            }

            return response;
        }
    };

    /// ==================== Download Info ====================

    /**
     * @brief 获取下载信息请求 DTO（路径参数）
     *
     * @details
     * 验证规则：
     * - file_id: 正整数
     *
     * 从 URL 路径参数解析。
     */
    struct DownloadInfoRequest : DtoBase<DownloadInfoRequest> {
        uint64_t file_id{ 0 };

        /// 从路径参数解析并验证，返回 Result
        [[nodiscard]]
        static auto FromPath(const std::string& file_id_str) -> Result<DownloadInfoRequest> {
            Logger::Debug() << "Start parsing download info request parameters";

            if (!file_id_str.empty() && file_id_str[0] == '-') {
                Logger::Warn() << "Parameter 'file_id' must be a positive integer: " << file_id_str;
                return std::unexpected(ErrorInfo(
                    ErrorCode::InvalidParameter,
                    "Parameter 'file_id' must be a positive integer"
                ));
            }

            auto file_id_result = ParsePositiveUInt64(file_id_str, "file_id");
            if (!file_id_result) return std::unexpected(file_id_result.error());

            DownloadInfoRequest request;
            request.file_id = *file_id_result;

            Logger::Debug() << "Parsed download info request: file_id=" << request.file_id;

            return request;
        }
    };

    /**
     * @brief 获取下载信息响应 DTO
     *
     * @details
     * 包含文件下载所需的元数据。
     */
    struct DownloadInfoResponse : DtoBase<DownloadInfoResponse> {
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
            SetField(json, "file_id", file_id);
            SetField(json, "filename", filename);
            SetField(json, "file_size", file_size);
            SetField(json, "file_hash", file_hash);
            SetField(json, "mime_type", mime_type);
            SetField(json, "supports_range", supports_range);
            return json;
        }
    };

    /**
     * @brief 文件详情响应 DTO
     *
     * @details
     * GET /api/file/{file_id} 返回的扁平文件详情字段。
     */
    struct FileDetailResponse : DtoBase<FileDetailResponse> {
        uint64_t id{ 0 };
        std::string name;
        std::string type; ///< "file" 或 "folder"
        int64_t size{ 0 };
        std::string hash; ///< MD5 hash
        std::string mime_type;
        uint64_t parent_id{ 0 };
        std::string path;
        std::string created_at;
        std::string updated_at;

        /// 转换为 JSON
        [[nodiscard]]
        auto ToJson() const -> Json::Value {
            Json::Value json;
            SetField(json, "id", id);
            SetField(json, "name", name);
            SetField(json, "type", type);
            SetField(json, "size", size);
            SetField(json, "hash", hash);
            SetField(json, "mime_type", mime_type);
            SetField(json, "parent_id", parent_id);
            SetField(json, "path", path);
            SetField(json, "created_at", created_at);
            SetField(json, "updated_at", updated_at);
            return json;
        }
    };

    /// ==================== Download ====================

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
    struct DownloadRequest : DtoBase<DownloadRequest> {
        uint64_t file_id{ 0 };

        /// 从路径参数解析并验证，返回 Result
        [[nodiscard]]
        static auto FromPath(const std::string& file_id_str) -> Result<DownloadRequest> {
            Logger::Debug() << "Start parsing download file request parameters";

            auto file_id_result = ParsePositiveUInt64(file_id_str, "file_id");
            if (!file_id_result) return std::unexpected(file_id_result.error());

            DownloadRequest request;
            request.file_id = *file_id_result;

            Logger::Debug() << "Parsed download file request: file_id=" << request.file_id;

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

    /// ==================== Rename ====================

    /**
     * @brief 重命名请求 DTO
     *
     * @details
     * 验证规则：
     * - file_id: 正整数（路径参数）
     * - new_name: 1-255字符，禁止字符 / \ : * ? " < > | 及控制字符
     * - new_name: 禁止保留名称 "." 和 ".."
     * - new_name: 禁止以 "." 开头（隐藏文件）
     * - new_name: 必须是合法 UTF-8，禁止控制字符
     *
     * 从 URL 路径参数和请求体解析。
     */
    struct RenameRequest : DtoBase<RenameRequest> {
        uint64_t file_id{ 0 };
        std::string new_name;

        /// 从路径参数和请求体解析并验证，返回 Result
        [[nodiscard]]
        static auto
        FromPathAndRequest(const std::string& file_id_str, const drogon::HttpRequestPtr& req)
            -> Result<RenameRequest> {
            Logger::Debug() << "Start parsing rename request parameters";

            /// 验证路径参数 file_id
            auto file_id_result = ParsePositiveUInt64(file_id_str, "file_id");
            if (!file_id_result) return std::unexpected(file_id_result.error());

            auto json_result = RequireJsonBody(req);
            if (!json_result) return std::unexpected(json_result.error());
            const auto& json = *json_result.value();

            auto new_name_result = RequireString(json, "new_name");
            if (!new_name_result) return std::unexpected(new_name_result.error());

            RenameRequest request;
            request.file_id = *file_id_result;
            request.new_name = std::move(*new_name_result);

            Logger::Debug() << "Parsed rename request: file_id=" << request.file_id << ", new_name=\""
                      << request.new_name << "\"";

            /// 验证新文件名
            if (!request.ValidateFilenameLength()) {
                Logger::Warn() << "Invalid filename length: " << request.new_name.length();
                return std::unexpected(ErrorInfo(
                    ErrorCode::ValidationFailed,
                    "Filename length must be between 1-255 characters"
                ));
            }

            if (!request.ValidateFilenameForbiddenChars()) {
                Logger::Warn() << "Filename contains forbidden characters: " << request.new_name;
                return std::unexpected(ErrorInfo(
                    ErrorCode::InvalidFilename,
                    "Filename contains forbidden characters: / \\ : * ? \" < > | or " "control " "c" "h" "a" "r" "a" "c" "t" "e" "r" "s"
                ));
            }

            if (!request.ValidateFilenameReservedNames()) {
                Logger::Warn() << "Filename is a reserved name: " << request.new_name;
                return std::unexpected(ErrorInfo(
                    ErrorCode::InvalidFilename,
                    "Filename cannot be reserved name \".\" or \"..\""
                ));
            }

            if (!request.ValidateFilenameNotHidden()) {
                Logger::Warn() << "Filename starts with a dot (hidden file): " << request.new_name;
                return std::unexpected(
                    ErrorInfo(ErrorCode::InvalidFilename, "Filename cannot start with \".\"")
                );
            }

            if (!request.ValidateFilenameCharset()) {
                Logger::Warn() << "Filename contains invalid UTF-8 or control characters: "
                         << request.new_name;
                return std::unexpected(ErrorInfo(
                    ErrorCode::InvalidFilename,
                    "Filename must be valid UTF-8 and cannot contain control characters"
                ));
            }

            Logger::Debug() << "Request parameters validation passed";
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
                /// 检查控制字符 (0x00-0x1F)
                if (static_cast<unsigned char>(c) <= 0x1F) {
                    return false;
                }
                /// 检查文件系统保留字符
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

        /// 验证文件名字符集（合法 UTF-8，且不含控制字符）
        [[nodiscard]]
        auto ValidateFilenameCharset() const -> bool {
            return utils::IsValidUtf8WithoutControlChars(new_name);
        }
    };

    /**
     * @brief 重命名响应 DTO
     *
     * @details
     * 包含重命名后的文件信息。
     */
    struct RenameResponse : DtoBase<RenameResponse> {
        uint64_t id{ 0 };
        std::string name;
        std::string updated_at;

        /// 转换为 JSON
        [[nodiscard]]
        auto ToJson() const -> Json::Value {
            Json::Value json;
            SetField(json, "id", id);
            SetField(json, "name", name);
            SetField(json, "updated_at", updated_at);
            return json;
        }
    };

    /// ==================== Move ====================

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
    struct MoveRequest : DtoBase<MoveRequest> {
        std::vector<uint64_t> file_ids;
        std::vector<uint64_t> folder_ids;
        uint64_t target_folder_id{ 0 };

        /// 从 HTTP 请求解析并验证，返回 Result
        [[nodiscard]]
        static auto FromRequest(const drogon::HttpRequestPtr& req) -> Result<MoveRequest> {
            Logger::Debug() << "Start parsing move drive items request parameters";

            auto json_result = RequireJsonBody(req);
            if (!json_result) return std::unexpected(json_result.error());
            const auto& json = *json_result.value();

            MoveRequest request;

            auto file_ids_result = OptionalPositiveIdArray(json, "file_ids");
            if (!file_ids_result) return std::unexpected(file_ids_result.error());
            request.file_ids = std::move(*file_ids_result);

            auto folder_ids_result = OptionalPositiveIdArray(json, "folder_ids");
            if (!folder_ids_result) return std::unexpected(folder_ids_result.error());
            request.folder_ids = std::move(*folder_ids_result);

            if (request.file_ids.empty() && request.folder_ids.empty()) {
                Logger::Warn() << "Move request must contain file_ids or folder_ids";
                return std::unexpected(ErrorInfo(
                    ErrorCode::InvalidParameter,
                    "Move request must contain file_ids or folder_ids"
                ));
            }

            /// 解析可选参数 target_folder_id
            auto target_folder_id_result = OptionalUInt64(json, "target_folder_id");
            if (!target_folder_id_result) return std::unexpected(target_folder_id_result.error());
            if (target_folder_id_result->has_value()) {
                request.target_folder_id = **target_folder_id_result;
            }

            Logger::Debug() << "Parsed move request: file_ids.size()=" << request.file_ids.size()
                      << ", folder_ids.size()=" << request.folder_ids.size()
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
    struct MoveResponse : DtoBase<MoveResponse> {
        int moved_count{ 0 };
        int moved_file_count{ 0 };
        int moved_folder_count{ 0 };

        /// 转换为 JSON
        [[nodiscard]]
        auto ToJson() const -> Json::Value {
            Json::Value json;
            SetField(json, "moved_count", moved_count);
            SetField(json, "moved_file_count", moved_file_count);
            SetField(json, "moved_folder_count", moved_folder_count);
            return json;
        }
    };

    /// ==================== Copy ====================

    /**
     * @brief 文件ID映射
     *
     * @details
     * 用于复制操作中记录旧ID和新ID的映射关系。
     */
    struct FileIdMapping : DtoBase<FileIdMapping> {
        uint64_t old_id{ 0 };
        uint64_t new_id{ 0 };

        /// 转换为 JSON
        [[nodiscard]]
        auto ToJson() const -> Json::Value {
            Json::Value json;
            SetField(json, "old_id", old_id);
            SetField(json, "new_id", new_id);
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
    struct CopyRequest : DtoBase<CopyRequest> {
        std::vector<uint64_t> file_ids;
        std::vector<uint64_t> folder_ids;
        uint64_t target_folder_id{ 0 };

        /// 从 HTTP 请求解析并验证，返回 Result
        [[nodiscard]]
        static auto FromRequest(const drogon::HttpRequestPtr& req) -> Result<CopyRequest> {
            Logger::Debug() << "Start parsing copy drive items request parameters";

            auto json_result = RequireJsonBody(req);
            if (!json_result) return std::unexpected(json_result.error());
            const auto& json = *json_result.value();

            CopyRequest request;

            auto file_ids_result = OptionalPositiveIdArray(json, "file_ids");
            if (!file_ids_result) return std::unexpected(file_ids_result.error());
            request.file_ids = std::move(*file_ids_result);

            auto folder_ids_result = OptionalPositiveIdArray(json, "folder_ids");
            if (!folder_ids_result) return std::unexpected(folder_ids_result.error());
            request.folder_ids = std::move(*folder_ids_result);

            if (request.file_ids.empty() && request.folder_ids.empty()) {
                Logger::Warn() << "Copy request must contain file_ids or folder_ids";
                return std::unexpected(ErrorInfo(
                    ErrorCode::InvalidParameter,
                    "Copy request must contain file_ids or folder_ids"
                ));
            }

            /// 解析可选参数 target_folder_id
            auto target_folder_id_result = OptionalUInt64(json, "target_folder_id");
            if (!target_folder_id_result) return std::unexpected(target_folder_id_result.error());
            if (target_folder_id_result->has_value()) {
                request.target_folder_id = **target_folder_id_result;
            }

            Logger::Debug() << "Parsed copy request: file_ids.size()=" << request.file_ids.size()
                      << ", folder_ids.size()=" << request.folder_ids.size()
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
    struct CopyResponse : DtoBase<CopyResponse> {
        int copied_count{ 0 };
        int copied_file_count{ 0 };
        int copied_folder_count{ 0 };
        std::vector<FileIdMapping> new_files;
        std::vector<FileIdMapping> new_folders;

        /// 转换为 JSON
        [[nodiscard]]
        auto ToJson() const -> Json::Value {
            Json::Value json;
            SetField(json, "copied_count", copied_count);
            SetField(json, "copied_file_count", copied_file_count);
            SetField(json, "copied_folder_count", copied_folder_count);
            SetArray(json, "new_files", new_files);
            SetArray(json, "new_folders", new_folders);
            return json;
        }
    };

    /// ==================== Delete ====================

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
    struct DeleteRequest : DtoBase<DeleteRequest> {
        std::vector<uint64_t> file_ids;
        std::vector<uint64_t> folder_ids;

        /// 从 HTTP 请求解析并验证，返回 Result
        [[nodiscard]]
        static auto FromRequest(const drogon::HttpRequestPtr& req) -> Result<DeleteRequest> {
            Logger::Debug() << "Start parsing delete file request parameters";

            auto json_result = RequireJsonBody(req);
            if (!json_result) return std::unexpected(json_result.error());
            const auto& json = *json_result.value();

            DeleteRequest request;

            auto file_ids_result = OptionalPositiveIdArray(json, "file_ids");
            if (!file_ids_result) return std::unexpected(file_ids_result.error());
            request.file_ids = std::move(*file_ids_result);

            auto folder_ids_result = OptionalPositiveIdArray(json, "folder_ids");
            if (!folder_ids_result) return std::unexpected(folder_ids_result.error());
            request.folder_ids = std::move(*folder_ids_result);

            if (request.file_ids.empty() && request.folder_ids.empty()) {
                Logger::Warn() << "Delete request requires at least one file_id or folder_id";
                return std::unexpected(ErrorInfo(
                    ErrorCode::InvalidParameter,
                    "At least one file_id or folder_id is required"
                ));
            }

            Logger::Debug() << "Parsed delete request: file_ids.size()=" << request.file_ids.size()
                      << ", folder_ids.size()=" << request.folder_ids.size();

            return request;
        }
    };

    /**
     * @brief 删除文件响应 DTO
     *
     * @details
     * 包含删除操作的结果统计。
     */
    struct DeleteResponse : DtoBase<DeleteResponse> {
        int deleted_count{ 0 };
        int deleted_file_count{ 0 };
        int deleted_folder_count{ 0 };

        /// 转换为 JSON
        [[nodiscard]]
        auto ToJson() const -> Json::Value {
            Json::Value json;
            SetField(json, "deleted_count", deleted_count);
            SetField(json, "deleted_file_count", deleted_file_count);
            SetField(json, "deleted_folder_count", deleted_folder_count);
            return json;
        }
    };

    /// ==================== DownloadInfo（内部服务使用）====================

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
        std::string file_hash;    ///< MD5 哈希
        std::string mime_type;
        std::string storage_path; ///< 文件物理存储路径
        bool supports_range{ true };
    };

    /// ==================== Search ====================

    /**
     * @brief 文件搜索查询参数
     *
     * @details
     * 封装文件搜索数据访问所需的关键词、过滤范围和分页参数。
     */
    struct SearchQueryParams {
        std::string keyword;
        std::string type{ "all" };
        std::optional<uint64_t> folder_id;
        int page{ 1 };
        int page_size{ 20 };

        [[nodiscard]]
        auto Offset() const -> int64_t {
            return static_cast<int64_t>(page - 1) * page_size;
        }
    };

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
    struct SearchRequest : DtoBase<SearchRequest> {
        SearchQueryParams query;

        /// 从 HTTP 请求查询参数解析并验证，返回 Result
        [[nodiscard]]
        static auto FromRequest(const drogon::HttpRequestPtr& req) -> Result<SearchRequest> {
            Logger::Debug() << "Start parsing file search request parameters";

            SearchRequest request;

            /// 有效类型
            static const std::set<std::string> valid_types = { "all", "file", "folder" };

            /// 解析必填参数 keyword
            auto keyword_str = req->getParameter("keyword");
            if (keyword_str.empty()) {
                Logger::Warn() << "Missing required parameter: keyword";
                return std::unexpected(
                    ErrorInfo(ErrorCode::ValidationFailed, "Missing required parameter: keyword")
                );
            }

            /// 验证 keyword 长度
            if (keyword_str.length() < 1 || keyword_str.length() > 100) {
                Logger::Warn() << "Parameter 'keyword' invalid length: " << keyword_str.length();
                return std::unexpected(ErrorInfo(
                    ErrorCode::ValidationFailed,
                    "Parameter 'keyword' length must be between 1-100 characters"
                ));
            }

            /// 过滤 keyword 中的特殊字符（防止 SQL 注入）
            for (char c : keyword_str) {
                if (c == '%' || c == '_' || c == '\\' || c == '\'' || c == '"') {
                    Logger::Warn() << "Parameter 'keyword' contains forbidden characters: " << c;
                    return std::unexpected(ErrorInfo(
                        ErrorCode::ValidationFailed,
                        "Parameter 'keyword' contains forbidden characters"
                    ));
                }
            }

            request.query.keyword = keyword_str;

            /// 解析可选参数 type
            auto type_str = req->getParameter("type");
            if (!type_str.empty()) {
                if (valid_types.find(type_str) == valid_types.end()) {
                    Logger::Warn() << "Parameter 'type' invalid value: " << type_str;
                    return std::unexpected(ErrorInfo(
                        ErrorCode::ValidationFailed,
                        "Parameter 'type' invalid value, must be all/file/folder"
                    ));
                }
                request.query.type = type_str;
            }

            /// 解析可选参数 folder_id
            auto folder_id_result = QueryUInt64(req, "folder_id");
            if (!folder_id_result) return std::unexpected(folder_id_result.error());
            request.query.folder_id = *folder_id_result;

            /// 解析可选参数 page
            auto page_result = QueryPositiveInt(req, "page", 1);
            if (!page_result) return std::unexpected(page_result.error());
            if (page_result->has_value()) {
                request.query.page = **page_result;
            }

            /// 解析可选参数 page_size
            auto page_size_result = QueryPositiveInt(req, "page_size", 1, 100);
            if (!page_size_result) return std::unexpected(page_size_result.error());
            if (page_size_result->has_value()) {
                request.query.page_size = **page_size_result;
            }

            Logger::Debug() << "Parsed file search request: keyword=\"" << request.query.keyword
                      << "\", type=" << request.query.type << ", folder_id="
                      << (request.query.folder_id.has_value() ? std::to_string(*request.query.folder_id) :
                                                                 "null")
                      << ", page=" << request.query.page << ", page_size=" << request.query.page_size;

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
            SetField(json, "path", path);
            return json;
        }
    };

    /**
     * @brief 文件搜索响应 DTO
     *
     * @details
     * 包含搜索结果项和分页信息。
     */
    struct SearchResponse : DtoBase<SearchResponse> {
        std::vector<SearchResultItem> items;
        Pagination pagination;

        /// 转换为 JSON
        [[nodiscard]]
        auto ToJson() const -> Json::Value {
            Json::Value json;
            SetArray(json, "items", items);
            SetField(json, "pagination", pagination);
            return json;
        }
    };

} ///< namespace disk::file
