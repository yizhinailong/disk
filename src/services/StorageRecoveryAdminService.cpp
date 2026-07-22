/**
 * @file StorageRecoveryAdminService.cpp
 * @brief Audited administrator recovery commands for uploads and storage
 */

#include "services/StorageRecoveryAdminService.hpp"

#include <algorithm>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#include "services/StorageJobContract.hpp"
#include "services/StorageJobRepository.hpp"
#include "services/TransactionRunner.hpp"
#include "services/UploadStateMachine.hpp"
#include "storage/UploadStagingStorage.hpp"
#include "utils/LogHelper.hpp"

namespace disk::recovery {
    namespace {
        template <typename Row, typename T>
        [[nodiscard]] auto OptionalValue(const Row& row, const char* field)
            -> std::optional<T> {
            return row[field].isNull() ? std::nullopt :
                                         std::optional(row[field].template as<T>());
        }

        [[nodiscard]] auto SerializeJson(const Json::Value& value) -> std::string {
            Json::StreamWriterBuilder builder;
            builder["indentation"] = "";
            return Json::writeString(builder, value);
        }

        auto SetLogContext(
            Json::Value& details,
            const disk::utils::LogContext& log_context
        ) -> void {
            details["request_id"] =
                log_context.request_id.has_value() && !log_context.request_id->empty() ?
                    Json::Value(*log_context.request_id) :
                    Json::Value(Json::nullValue);
            details["operation"] =
                log_context.operation.has_value() && !log_context.operation->empty() ?
                    Json::Value(*log_context.operation) :
                    Json::Value(Json::nullValue);
        }

        [[nodiscard]] auto Bounded(std::string value, size_t maximum) -> std::string {
            if (value.size() > maximum) {
                value.resize(maximum);
            }
            return value;
        }

        auto RecordAudit(
            const drogon::orm::DbClientPtr& client,
            const RecoveryAuditContext& audit,
            std::string_view action,
            std::string_view target_type,
            std::string_view target_name,
            Json::Value details,
            const disk::utils::LogContext& log_context
        ) -> drogon::Task<void> {
            SetLogContext(details, log_context);
            co_await client->execSqlCoro(
                "INSERT INTO operation_logs " "(user_id, action, target_type, target_id, target_name, details, " "ip_address, user_agent) " "VALUES ($1, $2, $3, NULL, $4, $5::jsonb, $6, NULLIF($7, ''))",
                static_cast<int64_t>(audit.operator_id),
                std::string(action),
                std::string(target_type),
                std::string(target_name),
                SerializeJson(details),
                Bounded(audit.ip_address, 45),
                Bounded(audit.user_agent, 512)
            );
        }

        template <typename Row>
        [[nodiscard]] auto ParseUploadStatus(const Row& row)
            -> disk::upload::UploadTaskStatus {
            auto status = disk::upload::UploadTaskStatusFromStorage(
                row["status"].template as<int>()
            );
            if (!status.has_value()) {
                throw std::runtime_error("Upload task contains an invalid status");
            }
            return status.value();
        }

        template <typename Row>
        [[nodiscard]] auto ParseJobStatus(const Row& row)
            -> disk::jobs::StorageJobStatus {
            auto status = disk::jobs::ParseStorageJobStatus(
                row["status"].template as<int16_t>()
            );
            if (!status.has_value()) {
                throw std::runtime_error("Storage job contains an invalid status");
            }
            return status.value();
        }

        template <typename Row>
        [[nodiscard]] auto LeaseResponseFromRow(
            const Row& row,
            const disk::admin::UploadLeaseReleaseRequest& request
        ) -> disk::admin::UploadLeaseReleaseResponse {
            const auto status = ParseUploadStatus(row);
            const auto state_version = row["state_version"].template as<uint64_t>();
            auto lease_owner = OptionalValue<Row, std::string>(row, "lease_owner");
            auto lease_expires_at = OptionalValue<Row, std::string>(row, "lease_expires_at");
            const auto version_matches =
                !request.expected_state_version.has_value() ||
                request.expected_state_version.value() == state_version;
            const auto owner_matches =
                !request.expected_lease_owner.has_value() ||
                request.expected_lease_owner == lease_owner;
            const auto lease_expired = row["lease_expired"].template as<bool>();
            const auto eligible =
                status == disk::upload::UploadTaskStatus::Finalizing &&
                lease_owner.has_value() && lease_expires_at.has_value() &&
                !lease_expired && version_matches && owner_matches;
            return disk::admin::UploadLeaseReleaseResponse{
                .upload_id = row["id"].template as<std::string>(),
                .dry_run = request.dry_run,
                .eligible = eligible,
                .released = false,
                .status = status,
                .state_version = state_version,
                .lease_owner = std::move(lease_owner),
                .lease_expires_at = std::move(lease_expires_at),
                .lease_expired = lease_expired,
            };
        }

        struct CleanupPlan final {
            disk::admin::UploadCleanupRebuildResponse response;
            disk::storage::UploadStagingSession session;
            disk::jobs::NewStorageJob job;
        };

        auto LoadCleanupPlan(
            const drogon::orm::DbClientPtr& client,
            const disk::admin::UploadCleanupRebuildRequest& request,
            bool lock_rows
        ) -> drogon::Task<Result<CleanupPlan>> {
            auto upload_sql = std::string(
                "SELECT id, status, state_version, staging_backend, " "COALESCE(staging_prefix, temp_path) AS staging_prefix " "FROM upload_tasks WHERE id = $1"
            );
            if (lock_rows) {
                upload_sql += " FOR UPDATE";
            }
            auto uploads = co_await client->execSqlCoro(upload_sql, request.upload_id);
            if (uploads.empty()) {
                co_return std::unexpected(ErrorInfo(
                    ErrorCode::ResourceNotFound,
                    "Upload task not found"
                ));
            }

            const auto& row = uploads[0];
            const auto status = ParseUploadStatus(row);
            const auto state_version = row["state_version"].as<uint64_t>();
            auto backend = disk::storage::ParseUploadStagingBackend(
                row["staging_backend"].as<std::string>()
            );
            if (!backend.has_value()) {
                throw std::runtime_error("Upload task contains an invalid staging backend");
            }
            disk::storage::UploadStagingSession session{
                .upload_id = row["id"].as<std::string>(),
                .backend = backend.value(),
                .prefix = row["staging_prefix"].as<std::string>(),
            };
            auto job = disk::jobs::BuildStagingCleanupJob(session);

            auto job_sql = std::string(
                "SELECT id, job_type, aggregate_id, status FROM storage_jobs " "WHERE dedupe_key = $1"
            );
            if (lock_rows) {
                job_sql += " FOR UPDATE";
            }
            auto jobs = co_await client->execSqlCoro(job_sql, job.dedupe_key);

            disk::admin::UploadCleanupRebuildResponse response{
                .upload_id = request.upload_id,
                .dry_run = request.dry_run,
                .status = status,
                .state_version = state_version,
            };
            if (jobs.empty()) {
                response.planned_action = "create";
            } else {
                const auto& existing = jobs[0];
                if (existing["job_type"].as<std::string>() != job.job_type ||
                    existing["aggregate_id"].as<std::string>() != job.aggregate_id) {
                    throw std::runtime_error(
                        "Staging cleanup dedupe key belongs to another aggregate"
                    );
                }
                response.job_id = existing["id"].as<uint64_t>();
                response.job_status = ParseJobStatus(existing);
                if (response.job_status == disk::jobs::StorageJobStatus::Succeeded) {
                    response.planned_action = "rearm_succeeded";
                }
            }

            const auto version_matches =
                !request.expected_state_version.has_value() ||
                request.expected_state_version.value() == state_version;
            response.eligible = disk::upload::IsTerminalStatus(status) && version_matches &&
                                response.planned_action != "none";
            co_return CleanupPlan{
                .response = std::move(response),
                .session = std::move(session),
                .job = std::move(job),
            };
        }

        [[nodiscard]] auto ReconciliationPageSize(
            disk::reconciliation::ReconciliationScope scope
        ) -> size_t {
            return scope == disk::reconciliation::ReconciliationScope::Contents ||
                           scope == disk::reconciliation::ReconciliationScope::Users ?
                       disk::reconciliation::kMaxDatabaseReconciliationPageSize :
                       disk::reconciliation::kMaxObjectReconciliationPageSize;
        }

        [[nodiscard]] auto FirstReconciliationJob(
            const disk::admin::StorageReconciliationEnqueueRequest& request
        ) -> disk::jobs::NewStorageJob {
            auto job = disk::jobs::BuildStorageReconcileJob(
                disk::reconciliation::ReconciliationPageRequest{
                    .scan_id = request.scan_id,
                    .scope = request.scope,
                    .limit = ReconciliationPageSize(request.scope),
                }
            );
            if (!job.has_value()) {
                throw std::runtime_error(job.error());
            }
            return std::move(job.value());
        }

        auto LoadReconciliationResponse(
            const drogon::orm::DbClientPtr& client,
            const disk::admin::StorageReconciliationEnqueueRequest& request,
            const disk::jobs::NewStorageJob& job,
            bool lock_row
        ) -> drogon::Task<disk::admin::StorageReconciliationEnqueueResponse> {
            auto sql = std::string(
                "SELECT id, job_type, aggregate_id, status FROM storage_jobs " "WHERE dedupe_key = $1"
            );
            if (lock_row) {
                sql += " FOR UPDATE";
            }
            auto rows = co_await client->execSqlCoro(sql, job.dedupe_key);
            disk::admin::StorageReconciliationEnqueueResponse response{
                .scan_id = request.scan_id,
                .scope = request.scope,
                .dry_run = request.dry_run,
                .eligible = rows.empty(),
                .page_size = ReconciliationPageSize(request.scope),
                .dedupe_key = job.dedupe_key,
            };
            if (!rows.empty()) {
                const auto& row = rows[0];
                if (row["job_type"].as<std::string>() != job.job_type ||
                    row["aggregate_id"].as<std::string>() != job.aggregate_id) {
                    throw std::runtime_error(
                        "Reconciliation dedupe key belongs to another aggregate"
                    );
                }
                response.job_id = row["id"].as<uint64_t>();
                response.job_status = ParseJobStatus(row);
            }
            co_return response;
        }

        [[nodiscard]] auto ValidAudit(const RecoveryAuditContext& audit) -> bool {
            return audit.operator_id > 0;
        }
    } // namespace

    StorageRecoveryAdminService::StorageRecoveryAdminService(
        drogon::orm::DbClientPtr db_client
    ) : m_db_client(std::move(db_client)) {
        if (m_db_client == nullptr) {
            throw std::invalid_argument("Storage recovery database client is required");
        }
    }

    auto StorageRecoveryAdminService::ReleaseUploadLease(
        const disk::admin::UploadLeaseReleaseRequest& request,
        const RecoveryAuditContext& audit,
        disk::utils::LogContext log_context
    ) const -> drogon::Task<Result<disk::admin::UploadLeaseReleaseResponse>> {
        log_context.upload_id = request.upload_id;
        if (request.dry_run) {
            try {
                auto rows = co_await m_db_client->execSqlCoro(
                    "SELECT id, status, state_version, lease_owner, lease_expires_at, " "COALESCE(lease_expires_at <= NOW(), FALSE) AS lease_expired " "FROM upload_tasks WHERE id = $1",
                    request.upload_id
                );
                if (rows.empty()) {
                    co_return std::unexpected(ErrorInfo(
                        ErrorCode::ResourceNotFound,
                        "Upload task not found"
                    ));
                }
                auto response = LeaseResponseFromRow(rows[0], request);
                log_context.state_version = response.state_version;
                log_context.lease_owner = response.lease_owner;
                Logger::Info(log_context) << "Upload lease release dry-run inspected";
                co_return response;
            } catch (const std::exception& error) {
                Logger::Error(log_context)
                    << "Upload lease release dry-run failed: upload_id="
                    << request.upload_id << ", error=" << error.what();
                co_return std::unexpected(ErrorInfo(
                    ErrorCode::InternalError,
                    "Failed to inspect upload lease"
                ));
            }
        }

        if (!ValidAudit(audit) || !request.confirm_upload_id.has_value() ||
            request.confirm_upload_id.value() != request.upload_id ||
            !request.expected_state_version.has_value() ||
            !request.expected_lease_owner.has_value() || request.reason.empty()) {
            co_return std::unexpected(ErrorInfo(
                ErrorCode::ValidationFailed,
                "Upload lease release confirmation is incomplete"
            ));
        }

        disk::admin::UploadLeaseReleaseResponse response;
        disk::file::TransactionRunner transaction(
            m_db_client,
            ErrorInfo(ErrorCode::InternalError, "Failed to release upload lease")
        );
        auto result = co_await transaction.Run(
            [&](const drogon::orm::DbClientPtr& client) -> drogon::Task<Result<void>> {
                auto rows = co_await client->execSqlCoro(
                    "UPDATE upload_tasks SET lease_expires_at = NOW(), " "state_version = state_version + 1, updated_at = NOW() " "WHERE id = $1 AND status = $2 AND lease_owner = $3 " "AND state_version = $4 AND lease_expires_at > NOW() " "RETURNING id, status, state_version, lease_owner, lease_expires_at, " "COALESCE(lease_expires_at <= NOW(), FALSE) AS lease_expired",
                    request.upload_id,
                    disk::upload::ToStorageValue(disk::upload::UploadTaskStatus::Finalizing),
                    request.expected_lease_owner.value(),
                    request.expected_state_version.value()
                );
                if (rows.empty()) {
                    co_return std::unexpected(ErrorInfo(
                        ErrorCode::ResourceConflict,
                        "Upload lease changed before release"
                    ));
                }

                response = LeaseResponseFromRow(rows[0], request);
                response.dry_run = false;
                response.eligible = true;
                response.released = true;

                Json::Value details(Json::objectValue);
                details["upload_id"] = request.upload_id;
                details["expected_lease_owner"] = request.expected_lease_owner.value();
                details["previous_state_version"] =
                    Json::UInt64(request.expected_state_version.value());
                details["new_state_version"] = Json::UInt64(response.state_version);
                details["reason"] = request.reason;
                auto audit_log_context = log_context;
                audit_log_context.state_version = response.state_version;
                audit_log_context.lease_owner = response.lease_owner;
                co_await RecordAudit(
                    client,
                    audit,
                    "admin.upload.lease_release",
                    "upload",
                    request.upload_id,
                    details,
                    audit_log_context
                );
                co_return {};
            }
        );
        if (!result) {
            co_return std::unexpected(result.error());
        }

        log_context.state_version = response.state_version;
        log_context.lease_owner = response.lease_owner;
        Logger::Info(log_context)
            << "Upload lease released by administrator: upload_id="
            << request.upload_id << ", operator_id=" << audit.operator_id
            << ", state_version=" << response.state_version;
        co_return response;
    }

    auto StorageRecoveryAdminService::RebuildUploadCleanup(
        const disk::admin::UploadCleanupRebuildRequest& request,
        const RecoveryAuditContext& audit,
        disk::utils::LogContext log_context
    ) const -> drogon::Task<Result<disk::admin::UploadCleanupRebuildResponse>> {
        log_context.upload_id = request.upload_id;
        if (request.dry_run) {
            try {
                auto plan = co_await LoadCleanupPlan(m_db_client, request, false);
                if (!plan) {
                    co_return std::unexpected(plan.error());
                }
                auto response = std::move(plan->response);
                log_context.state_version = response.state_version;
                log_context.job_id = response.job_id;
                Logger::Info(log_context) << "Upload cleanup rebuild dry-run inspected";
                co_return response;
            } catch (const std::exception& error) {
                Logger::Error(log_context)
                    << "Upload cleanup rebuild dry-run failed: upload_id="
                    << request.upload_id << ", error=" << error.what();
                co_return std::unexpected(ErrorInfo(
                    ErrorCode::InternalError,
                    "Failed to inspect upload cleanup task"
                ));
            }
        }

        if (!ValidAudit(audit) || !request.confirm_upload_id.has_value() ||
            request.confirm_upload_id.value() != request.upload_id ||
            !request.expected_state_version.has_value() || request.reason.empty()) {
            co_return std::unexpected(ErrorInfo(
                ErrorCode::ValidationFailed,
                "Upload cleanup rebuild confirmation is incomplete"
            ));
        }

        disk::admin::UploadCleanupRebuildResponse response;
        disk::file::TransactionRunner transaction(
            m_db_client,
            ErrorInfo(ErrorCode::InternalError, "Failed to rebuild upload cleanup task")
        );
        auto result = co_await transaction.Run(
            [&](const drogon::orm::DbClientPtr& client) -> drogon::Task<Result<void>> {
                auto plan = co_await LoadCleanupPlan(client, request, true);
                if (!plan) {
                    co_return std::unexpected(plan.error());
                }
                if (!plan->response.eligible) {
                    co_return std::unexpected(ErrorInfo(
                        ErrorCode::ResourceConflict,
                        "Upload cleanup task is not eligible for rebuild"
                    ));
                }

                disk::jobs::StorageJobRepository repository(m_db_client);
                const auto changed = co_await repository.EnqueueOrRearmSucceeded(
                    client,
                    plan->job
                );
                if (!changed) {
                    co_return std::unexpected(ErrorInfo(
                        ErrorCode::ResourceConflict,
                        "Upload cleanup task changed before rebuild"
                    ));
                }

                auto jobs = co_await client->execSqlCoro(
                    "SELECT id, status FROM storage_jobs WHERE dedupe_key = $1",
                    plan->job.dedupe_key
                );
                if (jobs.size() != 1) {
                    throw std::runtime_error("Rebuilt upload cleanup task is missing");
                }
                response = std::move(plan->response);
                response.dry_run = false;
                response.eligible = true;
                response.rebuilt = true;
                response.job_id = jobs[0]["id"].as<uint64_t>();
                response.job_status = ParseJobStatus(jobs[0]);

                Json::Value details(Json::objectValue);
                details["upload_id"] = request.upload_id;
                details["state_version"] = Json::UInt64(response.state_version);
                details["planned_action"] = response.planned_action;
                details["job_id"] = Json::UInt64(response.job_id.value());
                details["reason"] = request.reason;
                auto audit_log_context = log_context;
                audit_log_context.state_version = response.state_version;
                audit_log_context.job_id = response.job_id;
                co_await RecordAudit(
                    client,
                    audit,
                    "admin.upload.cleanup_rebuild",
                    "upload",
                    request.upload_id,
                    details,
                    audit_log_context
                );
                co_return {};
            }
        );
        if (!result) {
            co_return std::unexpected(result.error());
        }

        log_context.state_version = response.state_version;
        log_context.job_id = response.job_id;
        Logger::Info(log_context)
            << "Upload cleanup task rebuilt by administrator: upload_id="
            << request.upload_id << ", job_id=" << response.job_id.value()
            << ", operator_id=" << audit.operator_id;
        co_return response;
    }

    auto StorageRecoveryAdminService::EnqueueReconciliation(
        const disk::admin::StorageReconciliationEnqueueRequest& request,
        const RecoveryAuditContext& audit,
        disk::utils::LogContext log_context
    ) const -> drogon::Task<Result<disk::admin::StorageReconciliationEnqueueResponse>> {
        disk::jobs::NewStorageJob job;
        try {
            job = FirstReconciliationJob(request);
            if (request.dry_run) {
                auto response = co_await LoadReconciliationResponse(
                    m_db_client,
                    request,
                    job,
                    false
                );
                log_context.job_id = response.job_id;
                Logger::Info(log_context)
                    << "Storage reconciliation enqueue dry-run inspected";
                co_return response;
            }
        } catch (const std::exception& error) {
            Logger::Error(log_context)
                << "Storage reconciliation enqueue inspection failed: scan_id="
                << request.scan_id << ", error=" << error.what();
            co_return std::unexpected(ErrorInfo(
                ErrorCode::InternalError,
                "Failed to inspect storage reconciliation task"
            ));
        }

        if (!ValidAudit(audit) || !request.confirm_scan_id.has_value() ||
            request.confirm_scan_id.value() != request.scan_id || request.reason.empty()) {
            co_return std::unexpected(ErrorInfo(
                ErrorCode::ValidationFailed,
                "Storage reconciliation confirmation is incomplete"
            ));
        }

        disk::admin::StorageReconciliationEnqueueResponse response;
        disk::file::TransactionRunner transaction(
            m_db_client,
            ErrorInfo(ErrorCode::InternalError, "Failed to enqueue storage reconciliation")
        );
        auto result = co_await transaction.Run(
            [&](const drogon::orm::DbClientPtr& client) -> drogon::Task<Result<void>> {
                auto current = co_await LoadReconciliationResponse(
                    client,
                    request,
                    job,
                    true
                );
                if (!current.eligible) {
                    co_return std::unexpected(ErrorInfo(
                        ErrorCode::ResourceConflict,
                        "Storage reconciliation scan already exists"
                    ));
                }

                disk::jobs::StorageJobRepository repository(m_db_client);
                if (!co_await repository.Enqueue(client, job)) {
                    co_return std::unexpected(ErrorInfo(
                        ErrorCode::ResourceConflict,
                        "Storage reconciliation scan was concurrently enqueued"
                    ));
                }
                auto rows = co_await client->execSqlCoro(
                    "SELECT id, status FROM storage_jobs WHERE dedupe_key = $1",
                    job.dedupe_key
                );
                if (rows.size() != 1) {
                    throw std::runtime_error("Enqueued reconciliation task is missing");
                }

                response = std::move(current);
                response.dry_run = false;
                response.eligible = true;
                response.enqueued = true;
                response.job_id = rows[0]["id"].as<uint64_t>();
                response.job_status = ParseJobStatus(rows[0]);

                Json::Value details(Json::objectValue);
                details["scan_id"] = request.scan_id;
                details["scope"] =
                    std::string(disk::reconciliation::ToStorageValue(request.scope));
                details["job_id"] = Json::UInt64(response.job_id.value());
                details["page_size"] = Json::UInt64(response.page_size);
                details["reason"] = request.reason;
                auto audit_log_context = log_context;
                audit_log_context.job_id = response.job_id;
                co_await RecordAudit(
                    client,
                    audit,
                    "admin.storage.reconcile",
                    "reconciliation",
                    request.scan_id,
                    details,
                    audit_log_context
                );
                co_return {};
            }
        );
        if (!result) {
            co_return std::unexpected(result.error());
        }

        log_context.job_id = response.job_id;
        Logger::Info(log_context)
            << "Storage reconciliation enqueued by administrator: scan_id="
            << request.scan_id << ", scope="
            << disk::reconciliation::ToStorageValue(request.scope)
            << ", job_id=" << response.job_id.value()
            << ", operator_id=" << audit.operator_id;
        co_return response;
    }

} // namespace disk::recovery
