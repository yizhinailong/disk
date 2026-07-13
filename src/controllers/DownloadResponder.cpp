/**
 * @file DownloadResponder.cpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 统一下载响应构造工具实现
 *
 * @copyright Copyright (c) 2026
 *
 */

#include "DownloadResponder.hpp"

#include <algorithm>
#include <limits>
#include <memory>

#include <json/json.h>

#include "dtos/FileDto.hpp"
#include "utils/ErrorCode.hpp"
#include "utils/Response.hpp"

namespace disk::controllers {

    namespace {
        constexpr std::size_t DOWNLOAD_STREAM_CHUNK_BYTES = 64ULL * 1024ULL;

        /// sendfile 零拷贝阈值（256KB）。Drogon 内部对 >200KB 的文件使用 sendfile()，
        /// 此阈值确保路由到 newFileResponse 路径时一定命中 sendfile 分支。
        /// HTTPS 场景下 Drogon 的 newFileResponse 会自动从 sendfile 回退到 read+write
        /// （TLS 连接不支持 sendfile），因此无需显式 isOnSecureConnection() 检测。
        /// 大文件仍走 newFileResponse 路径，享受 Drogon 的内部 TLS 回退机制；
        /// 小文件或非本地 Blob 走 newStreamResponse 流式路径，减少内存占用并兼容对象存储。
        constexpr std::size_t SENDFILE_THRESHOLD_BYTES = 256ULL * 1024ULL;
    }

    auto BuildDownloadResponse(
        const DownloadParams& params,
        disk::storage::IBlobStore* blob_store
    ) -> drogon::Task<drogon::HttpResponsePtr> {

        const auto& file_size = params.file_size;

        /// 解析 Range 请求头
        auto range_request =
            disk::file::RangeRequest::Parse(params.range_header, file_size);

        /// (a) 无效 Range → 416 JSON 响应
        if (range_request.has_range && !range_request.satisfiable) {
            Logger::Warn() << "Range request not satisfiable: " << params.range_header
                     << ", file_size=" << file_size;

            auto resp = drogon::HttpResponse::newHttpResponse();
            resp->setStatusCode(drogon::HttpStatusCode::k416RequestedRangeNotSatisfiable);
            resp->setContentTypeCode(drogon::CT_APPLICATION_JSON);

            resp->addHeader("Content-Range", "bytes */" + std::to_string(file_size));

            Json::Value error_data;
            error_data["file_size"] = static_cast<Json::UInt64>(file_size);
            error_data["requested_range"] = params.range_header;
            error_data["reason"] = "Requested start position exceeds file size";

            Json::Value body;
            body["code"] = 10002;
            body["message"] = "Invalid request range";
            body["data"] = error_data;

            resp->setBody(body.toStyledString());
            co_return resp;
        }

        /// 计算 Range 范围
        uint64_t start = range_request.has_range ? range_request.start : 0;
        uint64_t end = range_request.has_range ? range_request.end : file_size - 1;
        uint64_t content_length = end - start + 1;

        /// ================================================================
        /// Path A: newFileResponse — 本地 Blob sendfile 零拷贝路径
        /// ================================================================
        /// 当传输内容 >= sendfile 阈值且 BlobStore 暴露本地路径时，使用 Drogon
        /// 内置的 newFileResponse。S3/MinIO 等对象存储不会暴露本地路径，会落到
        /// Path B 的 Blob range stream。
        if (content_length >= SENDFILE_THRESHOLD_BYTES) {
            auto exists_result = co_await blob_store->BlobExists(params.blob);
            if (!exists_result || !*exists_result) {
                co_return Response::Error(
                    ErrorInfo(ErrorCode::FileNotFound, "File not found")
                );
            }

            if (auto local_path = blob_store->GetLocalBlobPathForDownload(params.blob); local_path.has_value()) {
                drogon::HttpResponsePtr resp;

                if (range_request.has_range) {
                    /// Range 请求 → Drogon 自动设置 206 + Content-Range
                    resp = drogon::HttpResponse::newFileResponse(
                        local_path->string(),
                        static_cast<size_t>(start),
                        static_cast<size_t>(content_length),
                        true,  ///< setContentRange
                        params.filename,
                        drogon::CT_CUSTOM,
                        params.mime_type
                    );
                    Logger::Info() << "Sending partial content via local file response: start=" << start
                             << ", end=" << end << ", total=" << file_size;
                } else {
                    /// 全文件下载 → Drogon 自动设置 200
                    resp = drogon::HttpResponse::newFileResponse(
                        local_path->string(),
                        params.filename,
                        drogon::CT_CUSTOM,
                        params.mime_type
                    );
                    Logger::Info() << "Sending full file via local file response: size=" << file_size;
                }

                /// 补充 newFileResponse 未设置的响应头
                resp->addHeader("Accept-Ranges", "bytes");
                resp->addHeader(
                    "Content-Disposition", "attachment; filename=\"" + params.filename + "\""
                );
                if (!params.file_hash.empty()) {
                    resp->addHeader("ETag", "\"" + params.file_hash + "\"");
                }

                co_return resp;
            }
        }

        /// ================================================================
        /// Path B: newStreamResponse — Blob range 流式下载路径（对象存储/小文件）
        /// ================================================================
        auto open_result = co_await blob_store->OpenBlobRangeForRead(params.blob, start, content_length);
        if (!open_result) {
            Logger::Error() << "Cannot open blob stream for download: content_id=" << params.blob.content_id;
            co_return Response::Error(ErrorInfo(ErrorCode::FileNotFound, "Cannot open file"));
        }
        auto stream = std::move(*open_result);

        auto remaining = std::make_shared<uint64_t>(content_length);
        auto resp = drogon::HttpResponse::newStreamResponse(
            [stream, remaining](char* buffer, std::size_t suggested_length) -> std::size_t {
                if (!buffer) {
                    stream->Close();
                    return 0;
                }

                if (*remaining == 0) {
                    stream->Close();
                    return 0;
                }

                const auto max_remaining =
                    static_cast<uint64_t>(std::numeric_limits<std::size_t>::max());
                const auto bounded_remaining =
                    static_cast<std::size_t>(std::min<uint64_t>(*remaining, max_remaining));
                const auto read_size =
                    std::min({ suggested_length, DOWNLOAD_STREAM_CHUNK_BYTES, bounded_remaining });

                const auto read_bytes = stream->Read(buffer, read_size);
                if (read_bytes == 0) {
                    *remaining = 0;
                    stream->Close();
                    return 0;
                }

                *remaining -= read_bytes;
                if (*remaining == 0) {
                    stream->Close();
                }
                return read_bytes;
            }
        );

        /// (b) 有效 Range → 206
        if (range_request.has_range) {
            resp->setStatusCode(drogon::HttpStatusCode::k206PartialContent);
            resp->addHeader(
                "Content-Range",
                "bytes " + std::to_string(start) + "-" + std::to_string(end) + "/" +
                    std::to_string(file_size)
            );
            Logger::Info() << "Returning partial content: start=" << start << ", end=" << end
                     << ", total=" << file_size;
        } else {
            /// (c) 无 Range → 200 全文件
            resp->setStatusCode(drogon::HttpStatusCode::k200OK);
            Logger::Info() << "Returning full file: size=" << file_size;
        }

        /// 设置公共响应头
        resp->setContentTypeString(params.mime_type);
        resp->addHeader("Content-Length", std::to_string(content_length));
        resp->addHeader("Accept-Ranges", "bytes");
        resp->addHeader("Content-Disposition", "attachment; filename=\"" + params.filename + "\"");

        if (!params.file_hash.empty()) {
            resp->addHeader("ETag", "\"" + params.file_hash + "\"");
        }

        co_return resp;
    }

} ///< namespace disk::controllers
