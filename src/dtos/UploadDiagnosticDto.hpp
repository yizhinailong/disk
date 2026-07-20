/**
 * @file UploadDiagnosticDto.hpp
 * @brief Read-only administrator contracts for upload diagnostics
 */

#pragma once

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <drogon/HttpRequest.h>
#include <json/json.h>

#include "dtos/StorageJobAdminDto.hpp"
#include "services/UploadStateMachine.hpp"
#include "storage/UploadStagingStorage.hpp"
#include "utils/DtoBase.hpp"
#include "utils/ErrorCode.hpp"

namespace disk::admin {

    struct UploadDiagnosticRequest final : DtoBase<UploadDiagnosticRequest> {
        std::string upload_id;
        int chunk_page{ 1 };
        int chunk_page_size{ 20 };
        int job_page{ 1 };
        int job_page_size{ 20 };

        [[nodiscard]]
        static auto FromRequest(
            const drogon::HttpRequestPtr& request,
            std::string_view path_upload_id
        ) -> Result<UploadDiagnosticRequest> {
            if (path_upload_id.empty() || path_upload_id.size() > 64 ||
                !std::ranges::all_of(path_upload_id, IsAllowedUploadIdCharacter)) {
                return std::unexpected(ErrorInfo(
                    ErrorCode::ValidationFailed,
                    "Invalid upload ID"
                ));
            }

            UploadDiagnosticRequest result;
            result.upload_id = path_upload_id;
            auto chunk_page = QueryPositiveInt(request, "chunk_page", 1);
            if (!chunk_page) {
                return std::unexpected(chunk_page.error());
            }
            auto chunk_page_size = QueryPositiveInt(request, "chunk_page_size", 1, 100);
            if (!chunk_page_size) {
                return std::unexpected(chunk_page_size.error());
            }
            auto job_page = QueryPositiveInt(request, "job_page", 1);
            if (!job_page) {
                return std::unexpected(job_page.error());
            }
            auto job_page_size = QueryPositiveInt(request, "job_page_size", 1, 100);
            if (!job_page_size) {
                return std::unexpected(job_page_size.error());
            }

            result.chunk_page = chunk_page->value_or(result.chunk_page);
            result.chunk_page_size = chunk_page_size->value_or(result.chunk_page_size);
            result.job_page = job_page->value_or(result.job_page);
            result.job_page_size = job_page_size->value_or(result.job_page_size);
            return result;
        }

    private:
        [[nodiscard]]
        static constexpr auto IsAllowedUploadIdCharacter(char character) noexcept -> bool {
            return (character >= 'a' && character <= 'z') ||
                   (character >= 'A' && character <= 'Z') ||
                   (character >= '0' && character <= '9') || character == '.' ||
                   character == '_' || character == ':' || character == '-';
        }
    };

    struct UploadDiagnosticLease final {
        std::string owner;
        std::string expires_at;
        bool expired{ false };

        [[nodiscard]]
        auto ToJson() const -> Json::Value {
            Json::Value json(Json::objectValue);
            json["owner"] = owner;
            json["expires_at"] = expires_at;
            json["expired"] = expired;
            return json;
        }
    };

    struct UploadDiagnosticTask final {
        std::string upload_id;
        uint64_t user_id{ 0 };
        uint64_t folder_id{ 0 };
        std::string filename;
        uint64_t file_size{ 0 };
        std::string file_hash;
        uint32_t chunk_size{ 0 };
        uint32_t total_chunks{ 0 };
        uint64_t reserved_bytes{ 0 };
        disk::storage::UploadStagingBackend staging_backend{
            disk::storage::UploadStagingBackend::Local
        };
        std::string staging_prefix;
        disk::upload::UploadTaskStatus status{ disk::upload::UploadTaskStatus::InProgress };
        uint64_t state_version{ 0 };
        std::optional<UploadDiagnosticLease> lease;
        uint32_t finalize_attempts{ 0 };
        std::optional<int32_t> last_error_code;
        std::optional<std::string> last_error_at;
        std::optional<uint64_t> completed_file_id;
        std::string expires_at;
        std::optional<std::string> finalized_at;
        std::string created_at;
        std::string updated_at;

        [[nodiscard]]
        auto ToJson() const -> Json::Value {
            Json::Value json(Json::objectValue);
            json["upload_id"] = upload_id;
            json["user_id"] = Json::UInt64(user_id);
            json["folder_id"] = Json::UInt64(folder_id);
            json["filename"] = filename;
            json["file_size"] = Json::UInt64(file_size);
            json["file_hash"] = file_hash;
            json["chunk_size"] = chunk_size;
            json["total_chunks"] = total_chunks;
            json["reserved_bytes"] = Json::UInt64(reserved_bytes);
            json["staging_backend"] =
                std::string(disk::storage::ToStorageValue(staging_backend));
            json["staging_prefix"] = staging_prefix;
            json["status"] = std::string(disk::upload::UploadTaskStatusName(status));
            json["state_version"] = Json::UInt64(state_version);
            json["lease"] = lease.has_value() ? lease->ToJson() : Json::Value();
            json["finalize_attempts"] = finalize_attempts;
            json["last_error_code"] = last_error_code.has_value() ?
                                          Json::Value(*last_error_code) :
                                          Json::Value();
            json["last_error_at"] = last_error_at.has_value() ?
                                        Json::Value(*last_error_at) :
                                        Json::Value();
            json["completed_file_id"] = completed_file_id.has_value() ?
                                            Json::Value(Json::UInt64(*completed_file_id)) :
                                            Json::Value();
            json["expires_at"] = expires_at;
            json["finalized_at"] = finalized_at.has_value() ?
                                       Json::Value(*finalized_at) :
                                       Json::Value();
            json["created_at"] = created_at;
            json["updated_at"] = updated_at;
            return json;
        }
    };

    struct UploadDiagnosticObjectHead final {
        std::string status{ "error" };
        std::optional<uint64_t> size_bytes;
        std::optional<std::string> etag;
        std::optional<bool> matches_record;
        std::optional<uint32_t> error_code;

        [[nodiscard]]
        auto ToJson() const -> Json::Value {
            Json::Value json(Json::objectValue);
            json["status"] = status;
            json["size_bytes"] = size_bytes.has_value() ?
                                     Json::Value(Json::UInt64(*size_bytes)) :
                                     Json::Value();
            json["etag"] = etag.has_value() ? Json::Value(*etag) : Json::Value();
            json["matches_record"] = matches_record.has_value() ?
                                         Json::Value(*matches_record) :
                                         Json::Value();
            json["error_code"] = error_code.has_value() ?
                                     Json::Value(*error_code) :
                                     Json::Value();
            return json;
        }
    };

    struct UploadDiagnosticChunk final {
        uint32_t chunk_index{ 0 };
        std::optional<uint64_t> size_bytes;
        std::optional<std::string> hash_md5;
        std::optional<std::string> object_key;
        std::optional<std::string> etag;
        std::string uploaded_at;
        UploadDiagnosticObjectHead object_head;

        [[nodiscard]]
        auto ToJson() const -> Json::Value {
            Json::Value json(Json::objectValue);
            json["chunk_index"] = chunk_index;
            json["size_bytes"] = size_bytes.has_value() ?
                                     Json::Value(Json::UInt64(*size_bytes)) :
                                     Json::Value();
            json["hash_md5"] = hash_md5.has_value() ? Json::Value(*hash_md5) : Json::Value();
            json["object_key"] = object_key.has_value() ?
                                     Json::Value(*object_key) :
                                     Json::Value();
            json["etag"] = etag.has_value() ? Json::Value(*etag) : Json::Value();
            json["uploaded_at"] = uploaded_at;
            json["object_head"] = object_head.ToJson();
            return json;
        }
    };

    struct UploadDiagnosticResponse final {
        UploadDiagnosticTask task;
        std::vector<UploadDiagnosticChunk> chunks;
        int chunk_page{ 1 };
        int chunk_page_size{ 20 };
        uint64_t chunk_total{ 0 };
        uint64_t chunk_total_pages{ 0 };
        StorageJobListResponse related_jobs;

        [[nodiscard]]
        auto ToJson() const -> Json::Value {
            Json::Value json(Json::objectValue);
            json["task"] = task.ToJson();

            Json::Value chunk_values(Json::arrayValue);
            for (const auto& chunk : chunks) {
                chunk_values.append(chunk.ToJson());
            }
            json["chunks"] = std::move(chunk_values);

            Json::Value pagination(Json::objectValue);
            pagination["page"] = chunk_page;
            pagination["page_size"] = chunk_page_size;
            pagination["total"] = Json::UInt64(chunk_total);
            pagination["total_pages"] = Json::UInt64(chunk_total_pages);
            json["chunk_pagination"] = std::move(pagination);
            json["related_jobs"] = related_jobs.ToJson();
            return json;
        }
    };

} // namespace disk::admin
