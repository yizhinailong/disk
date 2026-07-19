/**
 * @file MetricsService.cpp
 * @brief Prometheus rendering and bounded database snapshots
 */

#include "services/MetricsService.hpp"

#include <algorithm>
#include <array>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <utility>

#include "services/StorageJobRepository.hpp"
#include "utils/ConfigMgr.hpp"
#include "utils/LogHelper.hpp"

namespace disk::metrics {
    namespace {
        constexpr std::array<std::string_view, kHttpStatusClassCount> kStatusClassNames{
            "1xx",
            "2xx",
            "3xx",
            "4xx",
            "5xx",
            "other",
        };
        constexpr std::array<std::string_view, kStorageJobTypeCount> kStorageJobTypeNames{
            disk::jobs::kStagingCleanupJobType,
            disk::jobs::kMultipartAbortJobType,
            disk::jobs::kBlobGcJobType,
            disk::jobs::kExpireUploadsJobType,
            disk::jobs::kExpireTrashJobType,
            disk::jobs::kStorageReconcileJobType,
            "unknown",
        };
        constexpr std::array<std::string_view, 5> kStorageJobStatusNames{
            "pending",
            "running",
            "retry",
            "succeeded",
            "dead_letter",
        };
        constexpr std::array<std::string_view, 6> kUploadTaskStatusNames{
            "in_progress",
            "completed",
            "cancelled",
            "expired",
            "finalizing",
            "failed",
        };
        constexpr std::array<std::string_view, kReconciliationFindingTypeCount>
            kReconciliationFindingTypeNames{
                "content_ref_count_mismatch",
                "zero_reference_content",
                "missing_final_blob",
                "final_blob_size_mismatch",
                "quota_used_mismatch",
                "quota_reserved_mismatch",
                "orphan_staging_object",
                "orphan_final_blob",
                "unknown",
            };

        [[nodiscard]] constexpr auto ToIndex(auto value) noexcept -> size_t {
            return static_cast<size_t>(value);
        }

        [[nodiscard]] auto ClassifyStatusCode(int status_code) noexcept -> HttpStatusClass {
            switch (status_code / 100) {
                case 1:
                    return HttpStatusClass::Informational;
                case 2:
                    return HttpStatusClass::Success;
                case 3:
                    return HttpStatusClass::Redirect;
                case 4:
                    return HttpStatusClass::ClientError;
                case 5:
                    return HttpStatusClass::ServerError;
                default:
                    return HttpStatusClass::Other;
            }
        }

        [[nodiscard]] auto StorageJobTypeIndex(std::string_view job_type) noexcept -> size_t {
            const auto match = std::ranges::find(kStorageJobTypeNames, job_type);
            return match == kStorageJobTypeNames.end() ? kStorageJobTypeNames.size() - 1 :
                                                         static_cast<size_t>(match - kStorageJobTypeNames.begin());
        }

        [[nodiscard]] auto EscapeLabel(std::string_view value) -> std::string {
            std::string escaped;
            escaped.reserve(value.size());
            for (const auto character : value) {
                switch (character) {
                    case '\\':
                        escaped += "\\\\";
                        break;
                    case '"':
                        escaped += "\\\"";
                        break;
                    case '\n':
                        escaped += "\\n";
                        break;
                    default:
                        escaped += character;
                        break;
                }
            }
            return escaped;
        }

        auto WriteMetricHeader(
            std::ostringstream& output,
            std::string_view name,
            std::string_view help,
            std::string_view type
        ) -> void {
            output << "# HELP " << name << ' ' << help << '\n';
            output << "# TYPE " << name << ' ' << type << '\n';
        }

        [[nodiscard]] auto ReadUInt64(
            const drogon::orm::Row& row,
            const char* field
        ) -> uint64_t {
            return row[field].isNull() ? 0 : row[field].as<uint64_t>();
        }

        [[nodiscard]] auto ReadDouble(
            const drogon::orm::Row& row,
            const char* field
        ) -> double {
            return row[field].isNull() ? 0.0 : row[field].as<double>();
        }
    } // namespace

    auto ClassifyHttpOperation(std::string_view path) noexcept -> HttpOperation {
        if (disk::runtime::IsHealthProbePath(path)) {
            return HttpOperation::Health;
        }
        if (path == "/metrics") {
            return HttpOperation::Metrics;
        }
        if (path.starts_with("/api/auth/")) {
            return HttpOperation::Auth;
        }
        if (path == "/api/file/upload/init") {
            return HttpOperation::UploadInit;
        }
        if (path == "/api/file/upload/chunk") {
            return HttpOperation::UploadChunk;
        }
        if (path == "/api/file/upload/complete") {
            return HttpOperation::UploadComplete;
        }
        if (path.starts_with("/api/file/upload/")) {
            return HttpOperation::UploadCancel;
        }
        if (path.starts_with("/api/file/download/") ||
            path.starts_with("/api/share/download/")) {
            return HttpOperation::Download;
        }
        if (path == "/api/share" || path.starts_with("/api/share/")) {
            return HttpOperation::Share;
        }
        if (path == "/api/admin" || path.starts_with("/api/admin/")) {
            return HttpOperation::Admin;
        }
        return HttpOperation::Other;
    }

    auto HttpOperationName(HttpOperation operation) noexcept -> std::string_view {
        constexpr std::array<std::string_view, kHttpOperationCount> names{
            "health",
            "metrics",
            "auth",
            "upload_init",
            "upload_chunk",
            "upload_complete",
            "upload_cancel",
            "download",
            "share",
            "admin",
            "other",
        };
        const auto index = ToIndex(operation);
        return index < names.size() ? names[index] : "other";
    }

    auto StorageJobOutcomeName(StorageJobOutcome outcome) noexcept -> std::string_view {
        constexpr std::array<std::string_view, kStorageJobOutcomeCount> names{
            "succeeded",
            "retry",
            "dead_letter",
            "ownership_lost",
        };
        const auto index = ToIndex(outcome);
        return index < names.size() ? names[index] : "ownership_lost";
    }

    auto ReconciliationFindingTypeIndex(std::string_view finding_type) noexcept -> size_t {
        const auto match = std::ranges::find(kReconciliationFindingTypeNames, finding_type);
        return match == kReconciliationFindingTypeNames.end() ?
                   kReconciliationFindingTypeNames.size() - 1 :
                   static_cast<size_t>(match - kReconciliationFindingTypeNames.begin());
    }

    auto MetricsRegistry::GetInstance() -> MetricsRegistry& {
        static MetricsRegistry instance;
        return instance;
    }

    auto MetricsRegistry::RecordHttpRequest(
        HttpOperation operation,
        int status_code,
        std::chrono::microseconds duration
    ) -> void {
        auto operation_index = ToIndex(operation);
        if (operation_index >= kHttpOperationCount) {
            operation_index = ToIndex(HttpOperation::Other);
        }
        const auto status_index = ToIndex(ClassifyStatusCode(status_code));
        const auto duration_us = static_cast<uint64_t>(std::max<int64_t>(0, duration.count()));
        const auto duration_seconds = static_cast<double>(duration_us) / 1'000'000.0;

        std::scoped_lock lock(m_mutex);
        m_snapshot.http_requests[operation_index][status_index]++;
        m_snapshot.http_duration_count[operation_index]++;
        m_snapshot.http_duration_microseconds[operation_index] += duration_us;
        for (size_t bucket = 0; bucket < kDurationBucketsSeconds.size(); ++bucket) {
            if (duration_seconds <= kDurationBucketsSeconds[bucket]) {
                m_snapshot.http_duration_buckets[operation_index][bucket]++;
            }
        }
    }

    auto MetricsRegistry::RecordStorageJob(
        std::string_view job_type,
        StorageJobOutcome outcome,
        std::chrono::microseconds duration,
        bool lease_takeover
    ) -> void {
        const auto job_type_index = StorageJobTypeIndex(job_type);
        auto outcome_index = ToIndex(outcome);
        if (outcome_index >= kStorageJobOutcomeCount) {
            outcome_index = ToIndex(StorageJobOutcome::OwnershipLost);
        }
        const auto duration_us = static_cast<uint64_t>(std::max<int64_t>(0, duration.count()));
        const auto duration_seconds = static_cast<double>(duration_us) / 1'000'000.0;

        std::scoped_lock lock(m_mutex);
        m_snapshot.storage_job_runs[job_type_index][outcome_index]++;
        m_snapshot.storage_job_duration_count[job_type_index]++;
        m_snapshot.storage_job_duration_microseconds[job_type_index] += duration_us;
        if (lease_takeover) {
            m_snapshot.storage_job_takeovers[job_type_index]++;
        }
        for (size_t bucket = 0; bucket < kDurationBucketsSeconds.size(); ++bucket) {
            if (duration_seconds <= kDurationBucketsSeconds[bucket]) {
                m_snapshot.storage_job_duration_buckets[job_type_index][bucket]++;
            }
        }
    }

    auto MetricsRegistry::Snapshot() const -> MetricsSnapshot {
        std::scoped_lock lock(m_mutex);
        return m_snapshot;
    }

    MetricsService::MetricsService(
        drogon::orm::DbClientPtr db_client,
        std::shared_ptr<disk::runtime::ProcessRuntimeState> runtime_state
    )
        : m_db_client(std::move(db_client)), m_runtime_state(std::move(runtime_state)) {
        if (m_db_client == nullptr || m_runtime_state == nullptr) {
            throw std::invalid_argument("Metrics service dependencies are required");
        }
    }

    auto MetricsService::Render() const -> drogon::Task<std::string> {
        DatabaseMetricsSnapshot database;
        try {
            auto result = co_await m_db_client->execSqlCoro(
                "SELECT " "  (SELECT COUNT(*) FROM storage_jobs WHERE status = 0) AS jobs_pending, " "  (SELECT COUNT(*) FROM storage_jobs WHERE status = 1) AS jobs_running, " "  (SELECT COUNT(*) FROM storage_jobs WHERE status = 2) AS jobs_retry, " "  (SELECT COUNT(*) FROM storage_jobs WHERE status = 3) AS jobs_succeeded, " "  (SELECT COUNT(*) FROM storage_jobs WHERE status = 4) AS jobs_dead_letter, " "  (SELECT COALESCE(EXTRACT(EPOCH FROM (NOW() - MIN(available_at))), 0) " "     FROM storage_jobs WHERE status IN (0, 2) AND available_at <= NOW()) " "    AS oldest_ready_job_age_seconds, " "  (SELECT COUNT(*) FROM storage_jobs WHERE status = 1 AND locked_until <= NOW()) " "    AS expired_job_leases, " "  (SELECT COUNT(*) FROM upload_tasks WHERE status = 0) AS uploads_in_progress, " "  (SELECT COUNT(*) FROM upload_tasks WHERE status = 1) AS uploads_completed, " "  (SELECT COUNT(*) FROM upload_tasks WHERE status = 2) AS uploads_cancelled, " "  (SELECT COUNT(*) FROM upload_tasks WHERE status = 3) AS uploads_expired, " "  (SELECT COUNT(*) FROM upload_tasks WHERE status = 4) AS uploads_finalizing, " "  (SELECT COUNT(*) FROM upload_tasks WHERE status = 5) AS uploads_failed"
            );
            if (result.size() != 1) {
                throw std::runtime_error("Metrics database snapshot returned no row");
            }
            const auto& row = result[0];
            database.storage_jobs = {
                ReadUInt64(row, "jobs_pending"),
                ReadUInt64(row, "jobs_running"),
                ReadUInt64(row, "jobs_retry"),
                ReadUInt64(row, "jobs_succeeded"),
                ReadUInt64(row, "jobs_dead_letter"),
            };
            database.upload_tasks = {
                ReadUInt64(row, "uploads_in_progress"),
                ReadUInt64(row, "uploads_completed"),
                ReadUInt64(row, "uploads_cancelled"),
                ReadUInt64(row, "uploads_expired"),
                ReadUInt64(row, "uploads_finalizing"),
                ReadUInt64(row, "uploads_failed"),
            };
            database.oldest_ready_job_age_seconds = ReadDouble(row, "oldest_ready_job_age_seconds");
            database.expired_job_leases = ReadUInt64(row, "expired_job_leases");
            auto schema = co_await m_db_client->execSqlCoro(
                "SELECT to_regclass('public.storage_reconciliation_findings') IS NOT NULL " "AS reconciliation_schema_ready"
            );
            const auto reconciliation_schema_ready =
                schema.size() == 1 && schema[0]["reconciliation_schema_ready"].as<bool>();
            if (reconciliation_schema_ready) {
                auto findings = co_await m_db_client->execSqlCoro(
                    "SELECT finding_type, COUNT(*) AS unresolved_findings " "FROM storage_reconciliation_findings WHERE resolved_at IS NULL " "GROUP BY finding_type"
                );
                for (const auto& finding : findings) {
                    const auto index = ReconciliationFindingTypeIndex(
                        finding["finding_type"].as<std::string>()
                    );
                    database.reconciliation_findings[index] +=
                        ReadUInt64(finding, "unresolved_findings");
                }
            }
            database.success = reconciliation_schema_ready;
        } catch (const std::exception&) {
            Logger::Warn() << "Metrics database snapshot failed: instance_id="
                           << m_runtime_state->InstanceId();
        }

        co_return RenderSnapshot(
            MetricsRegistry::GetInstance().Snapshot(),
            database,
            *m_runtime_state
        );
    }

    auto MetricsService::RenderSnapshot(
        const MetricsSnapshot& metrics,
        const DatabaseMetricsSnapshot& database,
        const disk::runtime::ProcessRuntimeState& runtime_state
    ) -> std::string {
        std::ostringstream output;
        output << std::setprecision(17);

        WriteMetricHeader(output, "disk_http_requests_total", "HTTP requests by bounded operation and status class.", "counter");
        for (size_t operation = 0; operation < kHttpOperationCount; ++operation) {
            for (size_t status = 0; status < kHttpStatusClassCount; ++status) {
                output << "disk_http_requests_total{operation=\""
                       << HttpOperationName(static_cast<HttpOperation>(operation))
                       << "\",status_class=\"" << kStatusClassNames[status] << "\"} "
                       << metrics.http_requests[operation][status] << '\n';
            }
        }

        WriteMetricHeader(output, "disk_http_request_duration_seconds", "HTTP request duration by bounded operation.", "histogram");
        for (size_t operation = 0; operation < kHttpOperationCount; ++operation) {
            const auto name = HttpOperationName(static_cast<HttpOperation>(operation));
            for (size_t bucket = 0; bucket < kDurationBucketsSeconds.size(); ++bucket) {
                output << "disk_http_request_duration_seconds_bucket{operation=\"" << name
                       << "\",le=\"" << kDurationBucketsSeconds[bucket] << "\"} "
                       << metrics.http_duration_buckets[operation][bucket] << '\n';
            }
            output << "disk_http_request_duration_seconds_bucket{operation=\"" << name
                   << "\",le=\"+Inf\"} " << metrics.http_duration_count[operation] << '\n';
            output << "disk_http_request_duration_seconds_sum{operation=\"" << name << "\"} "
                   << static_cast<double>(metrics.http_duration_microseconds[operation]) / 1'000'000.0
                   << '\n';
            output << "disk_http_request_duration_seconds_count{operation=\"" << name << "\"} "
                   << metrics.http_duration_count[operation] << '\n';
        }

        WriteMetricHeader(output, "disk_storage_job_runs_total", "Storage job outcomes by fixed job type.", "counter");
        for (size_t job_type = 0; job_type < kStorageJobTypeCount; ++job_type) {
            for (size_t outcome = 0; outcome < kStorageJobOutcomeCount; ++outcome) {
                output << "disk_storage_job_runs_total{job_type=\"" << kStorageJobTypeNames[job_type]
                       << "\",outcome=\""
                       << StorageJobOutcomeName(static_cast<StorageJobOutcome>(outcome)) << "\"} "
                       << metrics.storage_job_runs[job_type][outcome] << '\n';
            }
        }

        WriteMetricHeader(output, "disk_storage_job_duration_seconds", "Storage job execution duration by fixed job type.", "histogram");
        for (size_t job_type = 0; job_type < kStorageJobTypeCount; ++job_type) {
            for (size_t bucket = 0; bucket < kDurationBucketsSeconds.size(); ++bucket) {
                output << "disk_storage_job_duration_seconds_bucket{job_type=\""
                       << kStorageJobTypeNames[job_type] << "\",le=\""
                       << kDurationBucketsSeconds[bucket] << "\"} "
                       << metrics.storage_job_duration_buckets[job_type][bucket] << '\n';
            }
            output << "disk_storage_job_duration_seconds_bucket{job_type=\""
                   << kStorageJobTypeNames[job_type] << "\",le=\"+Inf\"} "
                   << metrics.storage_job_duration_count[job_type] << '\n';
            output << "disk_storage_job_duration_seconds_sum{job_type=\""
                   << kStorageJobTypeNames[job_type] << "\"} "
                   << static_cast<double>(metrics.storage_job_duration_microseconds[job_type]) /
                          1'000'000.0
                   << '\n';
            output << "disk_storage_job_duration_seconds_count{job_type=\""
                   << kStorageJobTypeNames[job_type] << "\"} "
                   << metrics.storage_job_duration_count[job_type] << '\n';
        }

        WriteMetricHeader(output, "disk_storage_job_takeovers_total", "Expired storage job lease takeovers.", "counter");
        for (size_t job_type = 0; job_type < kStorageJobTypeCount; ++job_type) {
            output << "disk_storage_job_takeovers_total{job_type=\"" << kStorageJobTypeNames[job_type]
                   << "\"} " << metrics.storage_job_takeovers[job_type] << '\n';
        }

        WriteMetricHeader(output, "disk_storage_jobs", "Persistent storage jobs by state.", "gauge");
        for (size_t status = 0; status < kStorageJobStatusNames.size(); ++status) {
            output << "disk_storage_jobs{status=\"" << kStorageJobStatusNames[status] << "\"} "
                   << database.storage_jobs[status] << '\n';
        }
        WriteMetricHeader(output, "disk_upload_tasks", "Upload tasks by state.", "gauge");
        for (size_t status = 0; status < kUploadTaskStatusNames.size(); ++status) {
            output << "disk_upload_tasks{status=\"" << kUploadTaskStatusNames[status] << "\"} "
                   << database.upload_tasks[status] << '\n';
        }
        WriteMetricHeader(output, "disk_storage_jobs_oldest_ready_age_seconds", "Age of the oldest ready storage job.", "gauge");
        output << "disk_storage_jobs_oldest_ready_age_seconds "
               << database.oldest_ready_job_age_seconds << '\n';
        WriteMetricHeader(output, "disk_storage_jobs_expired_leases", "Running storage jobs with expired leases.", "gauge");
        output << "disk_storage_jobs_expired_leases " << database.expired_job_leases << '\n';
        WriteMetricHeader(output, "disk_reconciliation_findings_unresolved", "Unresolved storage reconciliation findings.", "gauge");
        for (size_t type = 0; type < kReconciliationFindingTypeNames.size(); ++type) {
            output << "disk_reconciliation_findings_unresolved{finding_type=\""
                   << kReconciliationFindingTypeNames[type] << "\"} "
                   << database.reconciliation_findings[type] << '\n';
        }
        WriteMetricHeader(output, "disk_metrics_snapshot_success", "Whether the last database metric snapshot succeeded.", "gauge");
        output << "disk_metrics_snapshot_success " << (database.success ? 1 : 0) << '\n';

        WriteMetricHeader(output, "disk_process_info", "Disk process identity and role.", "gauge");
        output << "disk_process_info{instance_id=\"" << EscapeLabel(runtime_state.InstanceId())
               << "\",role=\"" << disk::utils::ProcessRoleName(runtime_state.Role()) << "\"} 1\n";
        WriteMetricHeader(output, "disk_process_initialized", "Whether process initialization completed.", "gauge");
        output << "disk_process_initialized " << (runtime_state.IsInitialized() ? 1 : 0) << '\n';
        WriteMetricHeader(output, "disk_process_draining", "Whether the process is draining.", "gauge");
        output << "disk_process_draining " << (runtime_state.IsDraining() ? 1 : 0) << '\n';
        WriteMetricHeader(output, "disk_process_business_requests_inflight", "In-flight business requests.", "gauge");
        output << "disk_process_business_requests_inflight "
               << runtime_state.BusinessRequestsInflight() << '\n';
        return output.str();
    }

} // namespace disk::metrics
