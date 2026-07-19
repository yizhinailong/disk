/**
 * @file MultipartUploadJournal.cpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief PostgreSQL multipart upload recovery journal implementation
 *
 * @copyright Copyright (c) 2026
 */

#include "services/MultipartUploadJournal.hpp"

#include <cstddef>
#include <stdexcept>
#include <string_view>
#include <utility>

#include <json/json.h>

#include "services/StorageJobRepository.hpp"

namespace disk::jobs {
    namespace {
        constexpr size_t kMaxObjectKeyLength = 1024;
        constexpr size_t kMaxUploadIdLength = 4096;
        constexpr size_t kMaxErrorLength = 2048;
        constexpr size_t kOwnerInstancePrefixLength = 59;
        constexpr uint32_t kMultipartAbortMaxAttempts = 8;

        [[nodiscard]] auto ValidateDescriptor(
            const disk::storage::MultipartUploadDescriptor& descriptor
        ) -> Result<void> {
            if (descriptor.key.empty() || descriptor.key.size() > kMaxObjectKeyLength ||
                descriptor.upload_id.empty() || descriptor.upload_id.size() > kMaxUploadIdLength) {
                return std::unexpected(
                    ErrorInfo(ErrorCode::ValidationFailed, "Invalid multipart upload descriptor")
                );
            }
            return {};
        }

        [[nodiscard]] auto SerializePayload(
            const disk::storage::MultipartUploadDescriptor& descriptor
        ) -> std::string {
            Json::Value payload(Json::objectValue);
            payload["backend"] = "s3";
            payload["key"] = descriptor.key;
            payload["upload_id"] = descriptor.upload_id;
            Json::StreamWriterBuilder builder;
            builder["indentation"] = "";
            return Json::writeString(builder, payload);
        }

        [[nodiscard]] auto DatabaseFailure(std::string_view action) -> ErrorInfo {
            return ErrorInfo(
                ErrorCode::InternalError,
                "Failed to " + std::string(action) + " multipart recovery task"
            );
        }
    } // namespace

    PostgresMultipartUploadJournal::PostgresMultipartUploadJournal(
        drogon::orm::DbClientPtr db_client,
        std::string instance_id,
        uint32_t lease_duration_seconds
    ) : m_db_client(std::move(db_client)),
        m_owner_prefix(instance_id.substr(0, kOwnerInstancePrefixLength)),
        m_lease_duration_seconds(lease_duration_seconds) {
        if (m_db_client == nullptr) {
            throw std::invalid_argument("Multipart upload journal requires a database client");
        }
        if (instance_id.empty() || instance_id.size() > 128) {
            throw std::invalid_argument("Multipart upload journal instance ID must contain 1 to 128 characters");
        }
        if (m_lease_duration_seconds < 3) {
            throw std::invalid_argument("Multipart upload journal lease must be at least 3 seconds");
        }
    }

    auto PostgresMultipartUploadJournal::OwnerFor(
        const disk::storage::MultipartUploadDescriptor& descriptor
    ) const -> std::string {
        return m_owner_prefix + ":mp:" + disk::storage::BuildMultipartUploadRecoveryId(descriptor);
    }

    auto PostgresMultipartUploadJournal::Track(
        const disk::storage::MultipartUploadDescriptor& descriptor
    ) -> Result<void> {
        auto validation = ValidateDescriptor(descriptor);
        if (!validation) {
            return validation;
        }

        const auto aggregate_id = disk::storage::BuildMultipartUploadRecoveryId(descriptor);
        try {
            auto result = m_db_client->execSqlSync(
                "INSERT INTO storage_jobs " "  (job_type, aggregate_id, dedupe_key, payload, status, attempts, " "   max_attempts, available_at, locked_by, locked_until) " "VALUES ($1, $2, $3, $4::jsonb, $5, 0, $6, NOW(), $7, " "        NOW() + ($8::integer * INTERVAL '1 second')) " "ON CONFLICT (dedupe_key) DO NOTHING RETURNING id",
                std::string(kMultipartAbortJobType),
                aggregate_id,
                disk::storage::BuildMultipartUploadRecoveryDedupeKey(descriptor),
                SerializePayload(descriptor),
                ToStorageValue(StorageJobStatus::Running),
                static_cast<int32_t>(kMultipartAbortMaxAttempts),
                OwnerFor(descriptor),
                static_cast<int32_t>(m_lease_duration_seconds)
            );
            if (result.size() != 1) {
                return std::unexpected(
                    ErrorInfo(ErrorCode::ResourceConflict, "Multipart recovery task already exists")
                );
            }
            return {};
        } catch (const std::exception&) {
            return std::unexpected(DatabaseFailure("track"));
        }
    }

    auto PostgresMultipartUploadJournal::Renew(
        const disk::storage::MultipartUploadDescriptor& descriptor
    ) -> Result<void> {
        auto validation = ValidateDescriptor(descriptor);
        if (!validation) {
            return validation;
        }

        try {
            auto result = m_db_client->execSqlSync(
                "UPDATE storage_jobs SET " "  locked_until = NOW() + ($1::integer * INTERVAL '1 second'), updated_at = NOW() " "WHERE job_type = $2 AND aggregate_id = $3 AND dedupe_key = $4 " "  AND status = $5 AND locked_by = $6 AND locked_until > NOW()",
                static_cast<int32_t>(m_lease_duration_seconds),
                std::string(kMultipartAbortJobType),
                disk::storage::BuildMultipartUploadRecoveryId(descriptor),
                disk::storage::BuildMultipartUploadRecoveryDedupeKey(descriptor),
                ToStorageValue(StorageJobStatus::Running),
                OwnerFor(descriptor)
            );
            if (result.affectedRows() != 1) {
                return std::unexpected(
                    ErrorInfo(ErrorCode::ResourceConflict, "Multipart recovery lease ownership was lost")
                );
            }
            return {};
        } catch (const std::exception&) {
            return std::unexpected(DatabaseFailure("renew"));
        }
    }

    auto PostgresMultipartUploadJournal::Resolve(
        const disk::storage::MultipartUploadDescriptor& descriptor
    ) -> Result<void> {
        auto validation = ValidateDescriptor(descriptor);
        if (!validation) {
            return validation;
        }

        const auto aggregate_id = disk::storage::BuildMultipartUploadRecoveryId(descriptor);
        const auto dedupe_key = disk::storage::BuildMultipartUploadRecoveryDedupeKey(descriptor);
        try {
            auto result = m_db_client->execSqlSync(
                "UPDATE storage_jobs SET " "  status = $1, locked_by = NULL, locked_until = NULL, last_error = NULL, " "  completed_at = NOW(), updated_at = NOW() " "WHERE job_type = $2 AND aggregate_id = $3 AND dedupe_key = $4 " "  AND status = $5 AND locked_by = $6",
                ToStorageValue(StorageJobStatus::Succeeded),
                std::string(kMultipartAbortJobType),
                aggregate_id,
                dedupe_key,
                ToStorageValue(StorageJobStatus::Running),
                OwnerFor(descriptor)
            );
            if (result.affectedRows() == 1) {
                return {};
            }

            auto existing = m_db_client->execSqlSync(
                "SELECT status FROM storage_jobs " "WHERE job_type = $1 AND aggregate_id = $2 AND dedupe_key = $3",
                std::string(kMultipartAbortJobType),
                aggregate_id,
                dedupe_key
            );
            if (existing.size() == 1 &&
                existing[0]["status"].as<int16_t>() == ToStorageValue(StorageJobStatus::Succeeded)) {
                return {};
            }
            return std::unexpected(
                ErrorInfo(ErrorCode::ResourceConflict, "Multipart recovery task ownership was lost")
            );
        } catch (const std::exception&) {
            return std::unexpected(DatabaseFailure("resolve"));
        }
    }

    auto PostgresMultipartUploadJournal::ReleaseForRetry(
        const disk::storage::MultipartUploadDescriptor& descriptor,
        const std::string& error
    ) -> Result<void> {
        auto validation = ValidateDescriptor(descriptor);
        if (!validation) {
            return validation;
        }

        try {
            auto result = m_db_client->execSqlSync(
                "UPDATE storage_jobs SET " "  status = $1, available_at = NOW(), locked_by = NULL, locked_until = NULL, " "  last_error = $2, updated_at = NOW() " "WHERE job_type = $3 AND aggregate_id = $4 AND dedupe_key = $5 " "  AND status = $6 AND locked_by = $7",
                ToStorageValue(StorageJobStatus::Retry),
                error.substr(0, kMaxErrorLength),
                std::string(kMultipartAbortJobType),
                disk::storage::BuildMultipartUploadRecoveryId(descriptor),
                disk::storage::BuildMultipartUploadRecoveryDedupeKey(descriptor),
                ToStorageValue(StorageJobStatus::Running),
                OwnerFor(descriptor)
            );
            if (result.affectedRows() != 1) {
                return std::unexpected(
                    ErrorInfo(ErrorCode::ResourceConflict, "Multipart recovery task ownership was lost")
                );
            }
            return {};
        } catch (const std::exception&) {
            return std::unexpected(DatabaseFailure("release"));
        }
    }

} // namespace disk::jobs
