/**
 * @file StorageJobRepository.cpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief PostgreSQL 持久存储任务队列原语实现
 *
 * @copyright Copyright (c) 2026
 */

#include "StorageJobRepository.hpp"

#include <algorithm>
#include <array>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace disk::jobs {
    namespace {
        constexpr size_t kMaxJobTypeLength = 64;
        constexpr size_t kMaxAggregateIdLength = 128;
        constexpr size_t kMaxDedupeKeyLength = 255;
        constexpr size_t kMaxInstanceIdLength = 128;
        constexpr size_t kMaxErrorLength = 2048;
        constexpr size_t kMaxClaimBatchSize = 1000;

        auto ValidateBoundedValue(
            std::string_view value,
            size_t maximum_length,
            std::string_view field_name
        ) -> void {
            if (value.empty() || value.size() > maximum_length) {
                throw std::invalid_argument(
                    std::string(field_name) + " must contain 1 to " +
                    std::to_string(maximum_length) + " characters"
                );
            }
        }

        auto ValidateNewJob(const NewStorageJob& job) -> void {
            ValidateBoundedValue(job.job_type, kMaxJobTypeLength, "Storage job type");
            ValidateBoundedValue(job.aggregate_id, kMaxAggregateIdLength, "Storage job aggregate ID");
            ValidateBoundedValue(job.dedupe_key, kMaxDedupeKeyLength, "Storage job dedupe key");
            if (job.max_attempts == 0) {
                throw std::invalid_argument("Storage job max attempts must be positive");
            }
        }

        auto ValidateLeaseArguments(
            const std::string& instance_id,
            uint32_t lease_duration_seconds
        ) -> void {
            ValidateBoundedValue(instance_id, kMaxInstanceIdLength, "Storage job instance ID");
            if (lease_duration_seconds == 0) {
                throw std::invalid_argument("Storage job lease duration must be positive");
            }
        }

        [[nodiscard]] auto SerializePayload(const Json::Value& payload) -> std::string {
            Json::StreamWriterBuilder builder;
            builder["indentation"] = "";
            return Json::writeString(builder, payload);
        }

        [[nodiscard]] auto ParsePayload(const std::string& payload) -> Json::Value {
            Json::CharReaderBuilder builder;
            Json::Value value;
            std::string errors;
            std::istringstream stream(payload);
            if (!Json::parseFromStream(builder, stream, &value, &errors)) {
                throw std::runtime_error("Storage job contains invalid JSON payload: " + errors);
            }
            return value;
        }

        template <typename Row>
        [[nodiscard]] auto ToStorageJob(const Row& row) -> StorageJob {
            const auto raw_status = row["status"].template as<int16_t>();
            const auto status = ParseStorageJobStatus(raw_status);
            if (!status.has_value()) {
                throw std::runtime_error("Storage job contains an invalid status");
            }

            return StorageJob{
                .id = row["id"].template as<uint64_t>(),
                .job_type = row["job_type"].template as<std::string>(),
                .aggregate_id = row["aggregate_id"].template as<std::string>(),
                .dedupe_key = row["dedupe_key"].template as<std::string>(),
                .payload = ParsePayload(row["payload_json"].template as<std::string>()),
                .status = status.value(),
                .attempts = row["attempts"].template as<uint32_t>(),
                .max_attempts = row["max_attempts"].template as<uint32_t>(),
                .locked_by = row["locked_by"].template as<std::string>(),
                .lease_takeover = row["lease_takeover"].template as<bool>(),
            };
        }
    } // namespace

    auto StorageJobStatusName(StorageJobStatus status) noexcept -> std::string_view {
        switch (status) {
            case StorageJobStatus::Pending:
                return "pending";
            case StorageJobStatus::Running:
                return "running";
            case StorageJobStatus::Retry:
                return "retry";
            case StorageJobStatus::Succeeded:
                return "succeeded";
            case StorageJobStatus::DeadLetter:
                return "dead_letter";
        }
        return "unknown";
    }

    auto ParseStorageJobStatus(std::string_view value) noexcept
        -> std::optional<StorageJobStatus> {
        constexpr std::array statuses{
            StorageJobStatus::Pending,
            StorageJobStatus::Running,
            StorageJobStatus::Retry,
            StorageJobStatus::Succeeded,
            StorageJobStatus::DeadLetter,
        };
        const auto match = std::ranges::find_if(statuses, [value](StorageJobStatus status) {
            return StorageJobStatusName(status) == value;
        });
        return match == statuses.end() ? std::nullopt : std::optional(*match);
    }

    auto IsKnownStorageJobType(std::string_view value) noexcept -> bool {
        constexpr std::array types{
            kStagingCleanupJobType,
            kMultipartAbortJobType,
            kBlobGcJobType,
            kExpireUploadsJobType,
            kExpireTrashJobType,
            kStorageReconcileJobType,
        };
        return std::ranges::find(types, value) != types.end();
    }

    StorageJobRepository::StorageJobRepository(drogon::orm::DbClientPtr db_client)
        : m_db_client(std::move(db_client)) {
    }

    auto StorageJobRepository::Enqueue(const NewStorageJob& job) const -> drogon::Task<bool> {
        co_return co_await Enqueue(m_db_client, job);
    }

    auto StorageJobRepository::Enqueue(
        const drogon::orm::DbClientPtr& client,
        const NewStorageJob& job
    ) const -> drogon::Task<bool> {
        ValidateNewJob(job);
        auto result = co_await client->execSqlCoro(
            "INSERT INTO storage_jobs " "  (job_type, aggregate_id, dedupe_key, payload, max_attempts) " "VALUES ($1, $2, $3, $4::jsonb, $5) " "ON CONFLICT (dedupe_key) DO NOTHING " "RETURNING id",
            job.job_type,
            job.aggregate_id,
            job.dedupe_key,
            SerializePayload(job.payload),
            static_cast<int32_t>(job.max_attempts)
        );
        co_return !result.empty();
    }

    auto StorageJobRepository::EnqueueOrRearmSucceeded(
        const drogon::orm::DbClientPtr& client,
        const NewStorageJob& job
    ) const -> drogon::Task<bool> {
        ValidateNewJob(job);
        auto result = co_await client->execSqlCoro(
            "INSERT INTO storage_jobs " "  (job_type, aggregate_id, dedupe_key, payload, max_attempts) " "VALUES ($1, $2, $3, $4::jsonb, $5) " "ON CONFLICT (dedupe_key) DO UPDATE SET " "  job_type = EXCLUDED.job_type, aggregate_id = EXCLUDED.aggregate_id, " "  payload = EXCLUDED.payload, status = $6, attempts = 0, " "  max_attempts = EXCLUDED.max_attempts, available_at = NOW(), " "  locked_by = NULL, locked_until = NULL, last_error = NULL, " "  completed_at = NULL, updated_at = NOW() " "WHERE storage_jobs.status = $7 " "  AND storage_jobs.job_type = EXCLUDED.job_type " "  AND storage_jobs.aggregate_id = EXCLUDED.aggregate_id " "RETURNING id",
            job.job_type,
            job.aggregate_id,
            job.dedupe_key,
            SerializePayload(job.payload),
            static_cast<int32_t>(job.max_attempts),
            ToStorageValue(StorageJobStatus::Pending),
            ToStorageValue(StorageJobStatus::Succeeded)
        );
        if (!result.empty()) {
            co_return true;
        }

        auto existing = co_await client->execSqlCoro(
            "SELECT job_type, aggregate_id FROM storage_jobs WHERE dedupe_key = $1",
            job.dedupe_key
        );
        if (existing.empty()) {
            throw std::runtime_error("Storage job conflict disappeared during enqueue");
        }
        if (existing[0]["job_type"].as<std::string>() != job.job_type ||
            existing[0]["aggregate_id"].as<std::string>() != job.aggregate_id) {
            throw std::runtime_error("Storage job dedupe key is owned by another aggregate");
        }
        co_return false;
    }

    auto StorageJobRepository::CheckBlobGcReferenceGate(
        const drogon::orm::DbClientPtr& client,
        uint64_t content_id
    ) const -> drogon::Task<BlobGcReferenceGate> {
        const auto aggregate_id = std::to_string(content_id);
        auto result = co_await client->execSqlCoro(
            "SELECT job_type, aggregate_id, status FROM storage_jobs " "WHERE dedupe_key = $1 FOR SHARE",
            "blob-gc:" + aggregate_id
        );
        if (result.empty()) {
            co_return BlobGcReferenceGate::Allowed;
        }

        const auto& row = result[0];
        if (row["job_type"].as<std::string>() != kBlobGcJobType ||
            row["aggregate_id"].as<std::string>() != aggregate_id) {
            throw std::runtime_error("Blob GC dedupe key is owned by an incompatible storage job");
        }

        const auto status = ParseStorageJobStatus(row["status"].as<int16_t>());
        if (!status.has_value()) {
            throw std::runtime_error("Blob GC job contains an invalid status");
        }
        co_return status.value() == StorageJobStatus::Succeeded ? BlobGcReferenceGate::Allowed : BlobGcReferenceGate::Blocked;
    }

    auto StorageJobRepository::ClaimReadyBatch(
        const std::string& instance_id,
        size_t limit,
        uint32_t lease_duration_seconds
    ) const -> drogon::Task<std::vector<StorageJob>> {
        ValidateLeaseArguments(instance_id, lease_duration_seconds);
        if (limit == 0) {
            co_return std::vector<StorageJob>{};
        }

        const auto bounded_limit = std::min(limit, kMaxClaimBatchSize);
        co_await m_db_client->execSqlCoro(
            "UPDATE storage_jobs SET " "  status = $1, locked_by = NULL, locked_until = NULL, " "  last_error = COALESCE(last_error, 'Worker lease expired after final attempt'), " "  updated_at = NOW() " "WHERE attempts >= max_attempts AND (" "  status IN ($2, $3) OR " "  (status = $4 AND locked_until <= NOW())" ")",
            ToStorageValue(StorageJobStatus::DeadLetter),
            ToStorageValue(StorageJobStatus::Pending),
            ToStorageValue(StorageJobStatus::Retry),
            ToStorageValue(StorageJobStatus::Running)
        );

        auto result = co_await m_db_client->execSqlCoro(
            "WITH candidates AS (" "  SELECT id, status = $3 AS lease_takeover FROM storage_jobs " "  WHERE attempts < max_attempts AND (" "    (status IN ($1, $2) AND available_at <= NOW()) OR " "    (status = $3 AND locked_until <= NOW())" "  ) " "  ORDER BY COALESCE(locked_until, available_at), id " "  FOR UPDATE SKIP LOCKED " "  LIMIT $4" ") " "UPDATE storage_jobs AS job SET " "  status = $3, attempts = job.attempts + 1, locked_by = $5, " "  locked_until = NOW() + ($6::integer * INTERVAL '1 second'), " "  updated_at = NOW() " "FROM candidates " "WHERE job.id = candidates.id " "RETURNING job.id, job.job_type, job.aggregate_id, job.dedupe_key, " "  job.payload::text AS payload_json, job.status, job.attempts, " "  job.max_attempts, job.locked_by, candidates.lease_takeover",
            ToStorageValue(StorageJobStatus::Pending),
            ToStorageValue(StorageJobStatus::Retry),
            ToStorageValue(StorageJobStatus::Running),
            static_cast<int64_t>(bounded_limit),
            instance_id,
            static_cast<int32_t>(lease_duration_seconds)
        );

        std::vector<StorageJob> jobs;
        jobs.reserve(result.size());
        for (const auto& row : result) {
            jobs.push_back(ToStorageJob(row));
        }
        co_return jobs;
    }

    auto StorageJobRepository::RenewLease(
        uint64_t job_id,
        const std::string& instance_id,
        uint32_t lease_duration_seconds
    ) const -> drogon::Task<bool> {
        ValidateLeaseArguments(instance_id, lease_duration_seconds);
        auto result = co_await m_db_client->execSqlCoro(
            "UPDATE storage_jobs SET " "  locked_until = NOW() + ($1::integer * INTERVAL '1 second'), " "  updated_at = NOW() " "WHERE id = $2 AND status = $3 AND locked_by = $4 AND locked_until > NOW()",
            static_cast<int32_t>(lease_duration_seconds),
            static_cast<int64_t>(job_id),
            ToStorageValue(StorageJobStatus::Running),
            instance_id
        );
        co_return result.affectedRows() == 1;
    }

    auto StorageJobRepository::MarkSucceeded(
        uint64_t job_id,
        const std::string& instance_id
    ) const -> drogon::Task<bool> {
        co_return co_await MarkSucceeded(m_db_client, job_id, instance_id);
    }

    auto StorageJobRepository::MarkSucceeded(
        const drogon::orm::DbClientPtr& client,
        uint64_t job_id,
        const std::string& instance_id
    ) const -> drogon::Task<bool> {
        ValidateBoundedValue(instance_id, kMaxInstanceIdLength, "Storage job instance ID");
        auto result = co_await client->execSqlCoro(
            "UPDATE storage_jobs SET " "  status = $1, locked_by = NULL, locked_until = NULL, last_error = NULL, " "  completed_at = NOW(), updated_at = NOW() " "WHERE id = $2 AND status = $3 AND locked_by = $4",
            ToStorageValue(StorageJobStatus::Succeeded),
            static_cast<int64_t>(job_id),
            ToStorageValue(StorageJobStatus::Running),
            instance_id
        );
        co_return result.affectedRows() == 1;
    }

    auto StorageJobRepository::MarkFailed(
        uint64_t job_id,
        const std::string& instance_id,
        const std::string& error,
        bool retryable,
        uint32_t retry_delay_seconds
    ) const -> drogon::Task<std::optional<StorageJobStatus>> {
        ValidateBoundedValue(instance_id, kMaxInstanceIdLength, "Storage job instance ID");
        const auto bounded_error = error.substr(0, kMaxErrorLength);
        auto result = co_await m_db_client->execSqlCoro(
            "UPDATE storage_jobs SET " "  status = CASE WHEN $1::boolean AND attempts < max_attempts " "    THEN $2::smallint ELSE $3::smallint END, " "  available_at = CASE " "    WHEN $1::boolean AND attempts < max_attempts " "    THEN NOW() + ($4::integer * INTERVAL '1 second') " "    ELSE available_at END, " "  locked_by = NULL, locked_until = NULL, last_error = $5, updated_at = NOW() " "WHERE id = $6 AND status = $7::smallint AND locked_by = $8 " "RETURNING status",
            retryable,
            ToStorageValue(StorageJobStatus::Retry),
            ToStorageValue(StorageJobStatus::DeadLetter),
            static_cast<int32_t>(retry_delay_seconds),
            bounded_error,
            static_cast<int64_t>(job_id),
            ToStorageValue(StorageJobStatus::Running),
            instance_id
        );
        if (result.empty()) {
            co_return std::nullopt;
        }

        co_return ParseStorageJobStatus(result[0]["status"].as<int16_t>());
    }

} // namespace disk::jobs
