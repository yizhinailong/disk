/**
 * @file UploadDiagnosticService.cpp
 * @brief Read-only administrator diagnostics for upload sessions
 */

#include "services/UploadDiagnosticService.hpp"

#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

#include "services/StorageJobAdminService.hpp"
#include "storage/UploadStagingStorage.hpp"
#include "utils/LogHelper.hpp"

namespace disk::upload {
    namespace {
        template <typename Row, typename T>
        [[nodiscard]] auto OptionalValue(const Row& row, const char* field)
            -> std::optional<T> {
            return row[field].isNull() ? std::nullopt :
                                         std::optional(row[field].template as<T>());
        }

        template <typename Row>
        [[nodiscard]] auto ParseTask(const Row& row) -> disk::admin::UploadDiagnosticTask {
            const auto status = UploadTaskStatusFromStorage(row["status"].template as<int>());
            if (!status.has_value()) {
                throw std::runtime_error("Upload task contains an invalid status");
            }
            const auto backend = disk::storage::ParseUploadStagingBackend(
                row["staging_backend"].template as<std::string>()
            );
            if (!backend.has_value()) {
                throw std::runtime_error("Upload task contains an invalid staging backend");
            }

            std::optional<disk::admin::UploadDiagnosticLease> lease;
            const auto lease_owner = OptionalValue<Row, std::string>(row, "lease_owner");
            const auto lease_expires_at =
                OptionalValue<Row, std::string>(row, "lease_expires_at");
            if (lease_owner.has_value() && lease_expires_at.has_value()) {
                lease = disk::admin::UploadDiagnosticLease{
                    .owner = lease_owner.value(),
                    .expires_at = lease_expires_at.value(),
                    .expired = row["lease_expired"].template as<bool>(),
                };
            }

            return disk::admin::UploadDiagnosticTask{
                .upload_id = row["id"].template as<std::string>(),
                .user_id = row["user_id"].template as<uint64_t>(),
                .folder_id = row["folder_id"].template as<uint64_t>(),
                .filename = row["filename"].template as<std::string>(),
                .file_size = row["file_size"].template as<uint64_t>(),
                .file_hash = row["file_hash"].template as<std::string>(),
                .chunk_size = row["chunk_size"].template as<uint32_t>(),
                .total_chunks = row["total_chunks"].template as<uint32_t>(),
                .reserved_bytes = row["reserved_bytes"].template as<uint64_t>(),
                .staging_backend = backend.value(),
                .staging_prefix = row["resolved_staging_prefix"].template as<std::string>(),
                .status = status.value(),
                .state_version = row["state_version"].template as<uint64_t>(),
                .lease = std::move(lease),
                .finalize_attempts = row["finalize_attempts"].template as<uint32_t>(),
                .last_error_code = OptionalValue<Row, int32_t>(row, "last_error_code"),
                .last_error_at = OptionalValue<Row, std::string>(row, "last_error_at"),
                .completed_file_id = OptionalValue<Row, uint64_t>(row, "completed_file_id"),
                .expires_at = row["expires_at"].template as<std::string>(),
                .finalized_at = OptionalValue<Row, std::string>(row, "finalized_at"),
                .created_at = row["created_at"].template as<std::string>(),
                .updated_at = row["updated_at"].template as<std::string>(),
            };
        }

        template <typename Row>
        [[nodiscard]] auto ParseChunk(const Row& row) -> disk::admin::UploadDiagnosticChunk {
            return disk::admin::UploadDiagnosticChunk{
                .chunk_index = row["chunk_index"].template as<uint32_t>(),
                .size_bytes = OptionalValue<Row, uint64_t>(row, "size_bytes"),
                .hash_md5 = OptionalValue<Row, std::string>(row, "hash_md5"),
                .object_key = OptionalValue<Row, std::string>(row, "object_key"),
                .etag = OptionalValue<Row, std::string>(row, "etag"),
                .uploaded_at = row["uploaded_at"].template as<std::string>(),
            };
        }

        [[nodiscard]] auto ToStorageChunk(const disk::admin::UploadDiagnosticChunk& chunk)
            -> disk::storage::UploadStagingChunk {
            return disk::storage::UploadStagingChunk{
                .chunk_index = chunk.chunk_index,
                .size_bytes = chunk.size_bytes.value_or(0),
                .md5_hash = chunk.hash_md5.value_or(""),
                .object_key = chunk.object_key.value_or(""),
                .etag = chunk.etag.value_or(""),
            };
        }

        [[nodiscard]] auto BuildObjectHead(
            const Result<disk::storage::UploadStagingObjectHead>& result,
            const disk::admin::UploadDiagnosticChunk& chunk,
            disk::storage::UploadStagingBackend backend
        ) -> disk::admin::UploadDiagnosticObjectHead {
            if (!result) {
                return disk::admin::UploadDiagnosticObjectHead{
                    .status = "error",
                    .error_code = result.error().CodeInt(),
                };
            }
            if (!result->exists) {
                return disk::admin::UploadDiagnosticObjectHead{
                    .status = "missing",
                    .matches_record = false,
                };
            }

            std::optional<bool> matches_record;
            if (backend == disk::storage::UploadStagingBackend::Local) {
                if (chunk.size_bytes.has_value() && result->size_bytes.has_value()) {
                    matches_record = chunk.size_bytes == result->size_bytes;
                }
            } else if (chunk.size_bytes.has_value() && chunk.etag.has_value() && result->size_bytes.has_value() && result->etag.has_value()) {
                matches_record = chunk.size_bytes == result->size_bytes &&
                                 chunk.etag == result->etag;
            }

            return disk::admin::UploadDiagnosticObjectHead{
                .status = "present",
                .size_bytes = result->size_bytes,
                .etag = result->etag,
                .matches_record = matches_record,
            };
        }
    } // namespace

    UploadDiagnosticService::UploadDiagnosticService(
        drogon::orm::DbClientPtr db_client,
        disk::storage::UploadStagingStorage* staging_storage
    )
        : m_db_client(std::move(db_client)),
          m_staging_storage(staging_storage) {
        if (m_db_client == nullptr) {
            throw std::invalid_argument("Upload diagnostic database client is required");
        }
        if (m_staging_storage == nullptr) {
            throw std::invalid_argument("Upload diagnostic staging storage is required");
        }
    }

    auto UploadDiagnosticService::Diagnose(
        const disk::admin::UploadDiagnosticRequest& request
    ) const -> drogon::Task<Result<disk::admin::UploadDiagnosticResponse>> {
        try {
            auto task_rows = co_await m_db_client->execSqlCoro(
                "SELECT id, user_id, folder_id, filename, file_size, file_hash, chunk_size, " "total_chunks, reserved_bytes, staging_backend, " "COALESCE(staging_prefix, temp_path) AS resolved_staging_prefix, status, " "state_version, lease_owner, lease_expires_at, " "COALESCE(lease_expires_at <= NOW(), FALSE) AS lease_expired, " "finalize_attempts, last_error_code, last_error_at, completed_file_id, " "expires_at, finalized_at, created_at, updated_at " "FROM upload_tasks WHERE id = $1",
                request.upload_id
            );
            if (task_rows.empty()) {
                co_return std::unexpected(ErrorInfo(
                    ErrorCode::ResourceNotFound,
                    "Upload task not found"
                ));
            }

            disk::admin::UploadDiagnosticResponse response;
            response.task = ParseTask(task_rows[0]);
            response.chunk_page = request.chunk_page;
            response.chunk_page_size = request.chunk_page_size;

            auto chunk_count = co_await m_db_client->execSqlCoro(
                "SELECT COUNT(*) AS total FROM upload_task_chunks WHERE task_id = $1",
                request.upload_id
            );
            response.chunk_total =
                chunk_count.empty() ? 0 : chunk_count[0]["total"].as<uint64_t>();
            response.chunk_total_pages = response.chunk_total == 0 ? 0 :
                                                                     (response.chunk_total +
                                                                      request.chunk_page_size - 1) /
                                                                         static_cast<uint64_t>(request.chunk_page_size);

            const auto chunk_offset =
                static_cast<int64_t>(request.chunk_page - 1) * request.chunk_page_size;
            auto chunk_rows = co_await m_db_client->execSqlCoro(
                "SELECT chunk_index, size_bytes, hash_md5, object_key, etag, uploaded_at " "FROM upload_task_chunks WHERE task_id = $1 " "ORDER BY chunk_index LIMIT $2 OFFSET $3",
                request.upload_id,
                static_cast<int64_t>(request.chunk_page_size),
                chunk_offset
            );

            response.chunks.reserve(chunk_rows.size());
            const disk::storage::UploadStagingSession staging_session{
                .upload_id = response.task.upload_id,
                .backend = response.task.staging_backend,
                .prefix = response.task.staging_prefix,
            };
            for (const auto& row : chunk_rows) {
                auto chunk = ParseChunk(row);
                auto head = co_await m_staging_storage->HeadChunkObject(
                    staging_session,
                    ToStorageChunk(chunk)
                );
                chunk.object_head = BuildObjectHead(head, chunk, response.task.staging_backend);
                response.chunks.push_back(std::move(chunk));
            }

            const auto multipart_staging_prefix =
                response.task.staging_backend == disk::storage::UploadStagingBackend::S3 ?
                    response.task.staging_prefix :
                    std::string{};
            disk::jobs::StorageJobAdminService job_service(m_db_client);
            auto related_jobs = co_await job_service.ListRelatedToUpload(
                request.upload_id,
                multipart_staging_prefix,
                request.job_page,
                request.job_page_size
            );
            if (!related_jobs) {
                co_return std::unexpected(related_jobs.error());
            }
            response.related_jobs = std::move(related_jobs.value());
            co_return response;
        } catch (const std::exception& error) {
            Logger::Error() << "Upload diagnostic failed: upload_id=" << request.upload_id
                            << ", error=" << error.what();
            co_return std::unexpected(ErrorInfo(
                ErrorCode::InternalError,
                "Failed to diagnose upload task"
            ));
        }
    }

} // namespace disk::upload
