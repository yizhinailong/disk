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
    }

    auto BuildDownloadResponse(
        const DownloadParams& params,
        disk::storage::IFileStorage* storage
    ) -> drogon::Task<drogon::HttpResponsePtr> {

        const auto& file_size = params.file_size;

        // 解析 Range 请求头
        auto range_request =
            disk::file::RangeRequest::Parse(params.range_header, file_size);

        // (a) 无效 Range → 416 JSON 响应
        if (range_request.has_range && !range_request.satisfiable) {
            LOG_WARN << "Range request not satisfiable: " << params.range_header
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

        // 计算 Range 范围
        uint64_t start = range_request.has_range ? range_request.start : 0;
        uint64_t end = range_request.has_range ? range_request.end : file_size - 1;
        uint64_t content_length = end - start + 1;

        // 打开文件流
        auto open_result = co_await storage->OpenForRead(params.storage_path);
        if (!open_result) {
            LOG_ERROR << "Cannot open file: " << params.storage_path;
            co_return Response::Error(ErrorInfo(ErrorCode::FileNotFound, "Cannot open file"));
        }
        auto file = std::move(*open_result);
        if (start > static_cast<uint64_t>(std::numeric_limits<std::streamoff>::max())) {
            LOG_ERROR << "Range start exceeds stream offset limit: " << start;
            co_return Response::Error(ErrorInfo(ErrorCode::ValidationFailed, "Invalid request range"));
        }
        file->seekg(static_cast<std::streamoff>(start));

        // (b)(c) 构造流式响应（206 或 200）
        auto remaining = std::make_shared<uint64_t>(content_length);
        auto resp = drogon::HttpResponse::newStreamResponse(
            [file, remaining](char* buffer, std::size_t suggested_length) -> std::size_t {
                if (!buffer) {
                    if (file->is_open()) {
                        file->close();
                    }
                    return 0;
                }

                if (*remaining == 0 || !file->is_open()) {
                    return 0;
                }

                const auto max_remaining =
                    static_cast<uint64_t>(std::numeric_limits<std::size_t>::max());
                const auto bounded_remaining =
                    static_cast<std::size_t>(std::min<uint64_t>(*remaining, max_remaining));
                const auto read_size =
                    std::min({ suggested_length, DOWNLOAD_STREAM_CHUNK_BYTES, bounded_remaining });

                file->read(buffer, static_cast<std::streamsize>(read_size));
                const auto read_bytes = static_cast<std::size_t>(file->gcount());
                if (read_bytes == 0) {
                    *remaining = 0;
                    if (file->is_open()) {
                        file->close();
                    }
                    return 0;
                }

                *remaining -= read_bytes;
                if (*remaining == 0 && file->is_open()) {
                    file->close();
                }
                return read_bytes;
            }
        );

        // (b) 有效 Range → 206
        if (range_request.has_range) {
            resp->setStatusCode(drogon::HttpStatusCode::k206PartialContent);
            resp->addHeader(
                "Content-Range",
                "bytes " + std::to_string(start) + "-" + std::to_string(end) + "/" +
                    std::to_string(file_size)
            );
            LOG_INFO << "Returning partial content: start=" << start << ", end=" << end
                     << ", total=" << file_size;
        } else {
            // (c) 无 Range → 200 全文件
            resp->setStatusCode(drogon::HttpStatusCode::k200OK);
            LOG_INFO << "Returning full file: size=" << file_size;
        }

        // 设置公共响应头
        resp->setContentTypeString(params.mime_type);
        resp->addHeader("Content-Length", std::to_string(content_length));
        resp->addHeader("Accept-Ranges", "bytes");
        resp->addHeader("Content-Disposition", "attachment; filename=\"" + params.filename + "\"");

        if (!params.file_hash.empty()) {
            resp->addHeader("ETag", "\"" + params.file_hash + "\"");
        }

        co_return resp;
    }

} // namespace disk::controllers
