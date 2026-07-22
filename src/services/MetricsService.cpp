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

#include <trantor/utils/ConcurrentTaskQueue.h>

#include "services/StorageJobRepository.hpp"
#include "services/StorageReconciliationService.hpp"
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
                disk::reconciliation::kMissingFinalBlobFindingType,
                disk::reconciliation::kFinalBlobSizeMismatchFindingType,
                disk::reconciliation::kFinalBlobReadInterruptedFindingType,
                "quota_used_mismatch",
                "quota_reserved_mismatch",
                disk::reconciliation::kUploadStagingMismatchFindingType,
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

        [[nodiscard]] auto IsUnsignedDecimalSegment(std::string_view segment) noexcept -> bool {
            return !segment.empty() && std::ranges::all_of(segment, [](char character) {
                return character >= '0' && character <= '9';
            });
        }

        [[nodiscard]] auto IsNumericFileDetailPath(std::string_view path) noexcept -> bool {
            constexpr std::string_view prefix = "/api/file/";
            if (!path.starts_with(prefix)) {
                return false;
            }
            return IsUnsignedDecimalSegment(path.substr(prefix.size()));
        }

        [[nodiscard]] auto IsNumericFileRenamePath(std::string_view path) noexcept -> bool {
            constexpr std::string_view prefix = "/api/file/";
            constexpr std::string_view suffix = "/rename";
            if (!path.starts_with(prefix) || !path.ends_with(suffix) ||
                path.size() <= prefix.size() + suffix.size()) {
                return false;
            }
            return IsUnsignedDecimalSegment(path.substr(
                prefix.size(),
                path.size() - prefix.size() - suffix.size()
            ));
        }

        [[nodiscard]] auto IsNumericFolderActionPath(
            std::string_view path,
            std::string_view suffix
        ) noexcept -> bool {
            constexpr std::string_view prefix = "/api/folder/";
            if (!path.starts_with(prefix) || !path.ends_with(suffix) ||
                path.size() <= prefix.size() + suffix.size()) {
                return false;
            }
            return IsUnsignedDecimalSegment(path.substr(
                prefix.size(),
                path.size() - prefix.size() - suffix.size()
            ));
        }

        [[nodiscard]] auto IsSingleSegmentPath(
            std::string_view path,
            std::string_view prefix
        ) noexcept -> bool {
            if (!path.starts_with(prefix)) {
                return false;
            }
            const auto segment = path.substr(prefix.size());
            return !segment.empty() && segment.find('/') == std::string_view::npos;
        }

        [[nodiscard]] auto IsShareDownloadPath(std::string_view path) noexcept -> bool {
            constexpr std::string_view prefix = "/api/share/download/";
            if (!path.starts_with(prefix)) {
                return false;
            }

            const auto remainder = path.substr(prefix.size());
            const auto share_separator = remainder.find('/');
            if (share_separator == std::string_view::npos || share_separator == 0) {
                return false;
            }

            const auto file_and_suffix = remainder.substr(share_separator + 1);
            const auto file_separator = file_and_suffix.find('/');
            if (file_separator == std::string_view::npos) {
                return !file_and_suffix.empty();
            }
            return file_separator > 0 && file_and_suffix.substr(file_separator) == "/info";
        }
    } // namespace

    auto ClassifyHttpOperation(std::string_view path) noexcept -> HttpOperation {
        if (disk::runtime::IsHealthProbePath(path)) {
            return HttpOperation::Health;
        }
        if (path == "/metrics") {
            return HttpOperation::Metrics;
        }
        if (path == "/api/auth/register" || path == "/api/auth/login" ||
            path == "/api/auth/refresh" || path == "/api/auth/logout") {
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
        if (path.starts_with("/api/file/download/") || IsShareDownloadPath(path)) {
            return HttpOperation::Download;
        }
        if (path == "/api/file/list" || path == "/api/file/search" ||
            IsNumericFileDetailPath(path)) {
            return HttpOperation::FileQuery;
        }
        if (path == "/api/file/move" || path == "/api/file/copy" ||
            path == "/api/file" || path == "/api/file/delete" ||
            IsNumericFileRenamePath(path)) {
            return HttpOperation::FileMutation;
        }
        if (path == "/api/folder/tree" ||
            IsNumericFolderActionPath(path, "/breadcrumb")) {
            return HttpOperation::FolderQuery;
        }
        if (path == "/api/folder/create" ||
            IsNumericFolderActionPath(path, "/rename")) {
            return HttpOperation::FolderMutation;
        }
        if (path == "/api/trash" || path == "/api/trash/restore" ||
            path == "/api/trash/delete" || path == "/api/trash/all") {
            return HttpOperation::Trash;
        }
        if (path == "/api/system/info") {
            return HttpOperation::SystemInfo;
        }
        if (path == "/api/user/profile" || path == "/api/user/password" ||
            path == "/api/user/storage") {
            return HttpOperation::User;
        }
        if (path == "/api/logs") {
            return HttpOperation::OperationLog;
        }
        if (path == "/api/share" || path == "/api/share/cancel" ||
            IsSingleSegmentPath(path, "/api/share/") ||
            IsSingleSegmentPath(path, "/api/share/access/") ||
            IsSingleSegmentPath(path, "/api/share/browse/") ||
            IsSingleSegmentPath(path, "/api/share/save/")) {
            return HttpOperation::Share;
        }
        if (path == "/api/admin/maintenance/cleanup/expired") {
            return HttpOperation::Cleanup;
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
            "file_query",
            "file_mutation",
            "folder_query",
            "folder_mutation",
            "trash",
            "system_info",
            "user",
            "operation_log",
            "upload_init",
            "upload_chunk",
            "upload_complete",
            "upload_cancel",
            "download",
            "share",
            "cleanup",
            "admin",
            "other",
        };
        const auto index = ToIndex(operation);
        return index < names.size() ? names[index] : "other";
    }

    auto UploadCompleteStageName(UploadCompleteStage stage) noexcept -> std::string_view {
        constexpr std::array<std::string_view, kUploadCompleteStageCount> names{
            "claim_lease",
            "load_metadata",
            "assemble",
            "dedup_lookup",
            "promote",
            "commit",
        };
        const auto index = ToIndex(stage);
        return index < names.size() ? names[index] : "unknown";
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

    auto DependencyName(Dependency dependency) noexcept -> std::string_view {
        constexpr std::array<std::string_view, kDependencyCount> names{
            "postgresql",
            "redis",
            "s3",
        };
        const auto index = ToIndex(dependency);
        return index < names.size() ? names[index] : "unknown";
    }

    auto DependencyOutcomeName(DependencyOutcome outcome) noexcept -> std::string_view {
        constexpr std::array<std::string_view, kDependencyOutcomeCount> names{
            "success",
            "timeout",
            "connection",
            "conflict",
            "not_found",
            "retryable",
            "permanent",
            "protocol",
            "other",
        };
        const auto index = ToIndex(outcome);
        return index < names.size() ? names[index] : "other";
    }

    auto ThreadQueueName(ThreadQueue queue) noexcept -> std::string_view {
        constexpr std::array<std::string_view, kThreadQueueCount> names{
            "local_file",
            "local_assembly",
            "local_blob",
            "s3",
        };
        const auto index = ToIndex(queue);
        return index < names.size() ? names[index] : "unknown";
    }

    auto ReconciliationFindingTypeIndex(std::string_view finding_type) noexcept -> size_t {
        const auto match = std::ranges::find(kReconciliationFindingTypeNames, finding_type);
        return match == kReconciliationFindingTypeNames.end() ?
                   kReconciliationFindingTypeNames.size() - 1 :
                   static_cast<size_t>(match - kReconciliationFindingTypeNames.begin());
    }

    DependencyCallTimer::DependencyCallTimer(Dependency dependency, bool uses_pool)
        : m_dependency(dependency),
          m_uses_pool(uses_pool),
          m_started_at(std::chrono::steady_clock::now()) {
        MetricsRegistry::GetInstance().BeginDependencyCall(m_dependency, m_uses_pool);
    }

    DependencyCallTimer::~DependencyCallTimer() {
        Finish(DependencyOutcome::Other);
    }

    auto DependencyCallTimer::Finish(DependencyOutcome outcome) noexcept -> void {
        if (m_finished) {
            return;
        }
        m_finished = true;
        MetricsRegistry::GetInstance().RecordDependencyCall(
            m_dependency,
            outcome,
            std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now() - m_started_at
            ),
            m_uses_pool
        );
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

    auto MetricsRegistry::RecordUploadChunk(uint64_t size_bytes) -> void {
        std::scoped_lock lock(m_mutex);
        m_snapshot.upload_chunks_total++;
        m_snapshot.upload_chunk_bytes_total += size_bytes;
    }

    auto MetricsRegistry::RecordUploadCompleteStage(
        UploadCompleteStage stage,
        std::chrono::microseconds duration
    ) -> void {
        const auto stage_index = ToIndex(stage);
        if (stage_index >= kUploadCompleteStageCount) {
            return;
        }
        const auto duration_us = static_cast<uint64_t>(std::max<int64_t>(0, duration.count()));
        const auto duration_seconds = static_cast<double>(duration_us) / 1'000'000.0;

        std::scoped_lock lock(m_mutex);
        m_snapshot.upload_complete_duration_count[stage_index]++;
        m_snapshot.upload_complete_duration_microseconds[stage_index] += duration_us;
        for (size_t bucket = 0; bucket < kUploadCompleteDurationBucketsSeconds.size(); ++bucket) {
            if (duration_seconds <= kUploadCompleteDurationBucketsSeconds[bucket]) {
                m_snapshot.upload_complete_duration_buckets[stage_index][bucket]++;
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

    auto MetricsRegistry::BeginDependencyCall(Dependency dependency, bool uses_pool) -> void {
        const auto dependency_index = ToIndex(dependency);
        if (dependency_index >= kDependencyCount) {
            return;
        }

        std::scoped_lock lock(m_mutex);
        m_snapshot.dependency_calls_inflight[dependency_index]++;
        if (uses_pool) {
            m_snapshot.dependency_pool_demand[dependency_index]++;
        }
    }

    auto MetricsRegistry::RecordDependencyCall(
        Dependency dependency,
        DependencyOutcome outcome,
        std::chrono::microseconds duration,
        bool uses_pool
    ) -> void {
        const auto dependency_index = ToIndex(dependency);
        auto outcome_index = ToIndex(outcome);
        if (dependency_index >= kDependencyCount) {
            return;
        }
        if (outcome_index >= kDependencyOutcomeCount) {
            outcome_index = ToIndex(DependencyOutcome::Other);
        }
        const auto duration_us = static_cast<uint64_t>(std::max<int64_t>(0, duration.count()));
        const auto duration_seconds = static_cast<double>(duration_us) / 1'000'000.0;

        std::scoped_lock lock(m_mutex);
        if (m_snapshot.dependency_calls_inflight[dependency_index] > 0) {
            m_snapshot.dependency_calls_inflight[dependency_index]--;
        }
        if (uses_pool && m_snapshot.dependency_pool_demand[dependency_index] > 0) {
            m_snapshot.dependency_pool_demand[dependency_index]--;
        }
        m_snapshot.dependency_calls[dependency_index][outcome_index]++;
        m_snapshot.dependency_duration_count[dependency_index]++;
        m_snapshot.dependency_duration_microseconds[dependency_index] += duration_us;
        for (size_t bucket = 0; bucket < kDurationBucketsSeconds.size(); ++bucket) {
            if (duration_seconds <= kDurationBucketsSeconds[bucket]) {
                m_snapshot.dependency_duration_buckets[dependency_index][bucket]++;
            }
        }
    }

    auto MetricsRegistry::AcquireDependencyPoolLease(Dependency dependency) -> void {
        const auto index = ToIndex(dependency);
        if (index >= kDependencyCount) {
            return;
        }
        std::scoped_lock lock(m_mutex);
        m_snapshot.dependency_pool_leases[index]++;
    }

    auto MetricsRegistry::ReleaseDependencyPoolLease(Dependency dependency) -> void {
        const auto index = ToIndex(dependency);
        if (index >= kDependencyCount) {
            return;
        }
        std::scoped_lock lock(m_mutex);
        if (m_snapshot.dependency_pool_leases[index] > 0) {
            m_snapshot.dependency_pool_leases[index]--;
        }
    }

    auto MetricsRegistry::SetDependencyPoolCapacity(Dependency dependency, uint64_t capacity) -> void {
        const auto index = ToIndex(dependency);
        if (index >= kDependencyCount) {
            return;
        }
        std::scoped_lock lock(m_mutex);
        m_snapshot.dependency_pool_capacity[index] = capacity;
    }

    auto MetricsRegistry::RegisterThreadQueue(
        ThreadQueue queue,
        const std::shared_ptr<trantor::ConcurrentTaskQueue>& task_queue,
        uint64_t workers
    ) -> void {
        const auto index = ToIndex(queue);
        if (index >= kThreadQueueCount) {
            return;
        }
        std::scoped_lock lock(m_mutex);
        m_thread_queues[index] = task_queue;
        m_snapshot.thread_queue_workers[index] = workers;
    }

    auto MetricsRegistry::Snapshot() const -> MetricsSnapshot {
        MetricsSnapshot snapshot;
        std::array<std::weak_ptr<trantor::ConcurrentTaskQueue>, kThreadQueueCount> queues;
        {
            std::scoped_lock lock(m_mutex);
            snapshot = m_snapshot;
            queues = m_thread_queues;
        }
        for (size_t index = 0; index < queues.size(); ++index) {
            if (const auto queue = queues[index].lock(); queue != nullptr) {
                snapshot.thread_queue_depth[index] = queue->getTaskCount();
            }
        }
        return snapshot;
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

    auto MetricsService::Render(
        disk::utils::LogContext log_context
    ) const -> drogon::Task<std::string> {
        auto& registry = MetricsRegistry::GetInstance();
        const auto config = disk::utils::ConfigMgr::GetInstance();
        registry.SetDependencyPoolCapacity(
            Dependency::PostgreSql,
            static_cast<uint64_t>(std::max<int64_t>(0, config->GetDbPoolSize()))
        );
        registry.SetDependencyPoolCapacity(
            Dependency::Redis,
            static_cast<uint64_t>(std::max<int64_t>(0, config->GetRedisPoolSize()))
        );
        const auto uses_s3 = config->GetStorageBackend() == disk::utils::StorageBackend::S3 ||
                             config->GetUploadStagingBackend() == disk::utils::StorageBackend::S3;
        registry.SetDependencyPoolCapacity(
            Dependency::S3,
            uses_s3 ? config->GetS3StorageConfig().max_connections : 0
        );

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
            Logger::Warn(log_context) << "Metrics database snapshot failed";
        }

        co_return RenderSnapshot(
            registry.Snapshot(),
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

        WriteMetricHeader(output, "disk_dependency_calls_total", "Dependency calls by fixed dependency and outcome.", "counter");
        for (size_t dependency = 0; dependency < kDependencyCount; ++dependency) {
            for (size_t outcome = 0; outcome < kDependencyOutcomeCount; ++outcome) {
                output << "disk_dependency_calls_total{dependency=\""
                       << DependencyName(static_cast<Dependency>(dependency))
                       << "\",outcome=\""
                       << DependencyOutcomeName(static_cast<DependencyOutcome>(outcome)) << "\"} "
                       << metrics.dependency_calls[dependency][outcome] << '\n';
            }
        }

        WriteMetricHeader(output, "disk_dependency_call_duration_seconds", "Dependency call duration by fixed dependency.", "histogram");
        for (size_t dependency = 0; dependency < kDependencyCount; ++dependency) {
            const auto name = DependencyName(static_cast<Dependency>(dependency));
            for (size_t bucket = 0; bucket < kDurationBucketsSeconds.size(); ++bucket) {
                output << "disk_dependency_call_duration_seconds_bucket{dependency=\"" << name
                       << "\",le=\"" << kDurationBucketsSeconds[bucket] << "\"} "
                       << metrics.dependency_duration_buckets[dependency][bucket] << '\n';
            }
            output << "disk_dependency_call_duration_seconds_bucket{dependency=\"" << name
                   << "\",le=\"+Inf\"} " << metrics.dependency_duration_count[dependency]
                   << '\n';
            output << "disk_dependency_call_duration_seconds_sum{dependency=\"" << name
                   << "\"} "
                   << static_cast<double>(metrics.dependency_duration_microseconds[dependency]) /
                          1'000'000.0
                   << '\n';
            output << "disk_dependency_call_duration_seconds_count{dependency=\"" << name
                   << "\"} " << metrics.dependency_duration_count[dependency] << '\n';
        }

        WriteMetricHeader(output, "disk_dependency_calls_inflight", "Dependency calls currently in flight.", "gauge");
        WriteMetricHeader(output, "disk_dependency_pool_capacity", "Configured dependency connection capacity for this process.", "gauge");
        WriteMetricHeader(output, "disk_dependency_pool_demand", "Application-observed pool demand including held transaction leases.", "gauge");
        WriteMetricHeader(output, "disk_dependency_pool_utilization_ratio", "Observed pool demand divided by configured capacity, capped at one.", "gauge");
        for (size_t dependency = 0; dependency < kDependencyCount; ++dependency) {
            const auto name = DependencyName(static_cast<Dependency>(dependency));
            const auto capacity = metrics.dependency_pool_capacity[dependency];
            const auto demand = metrics.dependency_pool_demand[dependency] +
                                metrics.dependency_pool_leases[dependency];
            const auto utilization = capacity == 0 ? 0.0 :
                                                     static_cast<double>(std::min(demand, capacity)) /
                                                         static_cast<double>(capacity);
            output << "disk_dependency_calls_inflight{dependency=\"" << name << "\"} "
                   << metrics.dependency_calls_inflight[dependency] << '\n';
            output << "disk_dependency_pool_capacity{dependency=\"" << name << "\"} "
                   << capacity << '\n';
            output << "disk_dependency_pool_demand{dependency=\"" << name << "\"} "
                   << demand << '\n';
            output << "disk_dependency_pool_utilization_ratio{dependency=\"" << name
                   << "\"} " << utilization << '\n';
        }

        WriteMetricHeader(output, "disk_thread_queue_depth", "Tasks waiting to start in fixed blocking I/O queues.", "gauge");
        WriteMetricHeader(output, "disk_thread_queue_workers", "Worker threads configured for fixed blocking I/O queues.", "gauge");
        for (size_t queue = 0; queue < kThreadQueueCount; ++queue) {
            const auto name = ThreadQueueName(static_cast<ThreadQueue>(queue));
            output << "disk_thread_queue_depth{queue=\"" << name << "\"} "
                   << metrics.thread_queue_depth[queue] << '\n';
            output << "disk_thread_queue_workers{queue=\"" << name << "\"} "
                   << metrics.thread_queue_workers[queue] << '\n';
        }

        WriteMetricHeader(output, "disk_upload_chunks_total", "Upload chunk requests accepted by staging and PostgreSQL.", "counter");
        output << "disk_upload_chunks_total " << metrics.upload_chunks_total << '\n';
        WriteMetricHeader(output, "disk_upload_chunk_bytes_total", "Upload chunk payload bytes accepted by staging and PostgreSQL.", "counter");
        output << "disk_upload_chunk_bytes_total " << metrics.upload_chunk_bytes_total << '\n';

        WriteMetricHeader(output, "disk_upload_complete_stage_duration_seconds", "Upload completion duration by fixed stage.", "histogram");
        for (size_t stage = 0; stage < kUploadCompleteStageCount; ++stage) {
            const auto name = UploadCompleteStageName(static_cast<UploadCompleteStage>(stage));
            for (size_t bucket = 0; bucket < kUploadCompleteDurationBucketsSeconds.size(); ++bucket) {
                output << "disk_upload_complete_stage_duration_seconds_bucket{stage=\"" << name
                       << "\",le=\"" << kUploadCompleteDurationBucketsSeconds[bucket] << "\"} "
                       << metrics.upload_complete_duration_buckets[stage][bucket] << '\n';
            }
            output << "disk_upload_complete_stage_duration_seconds_bucket{stage=\"" << name
                   << "\",le=\"+Inf\"} " << metrics.upload_complete_duration_count[stage]
                   << '\n';
            output << "disk_upload_complete_stage_duration_seconds_sum{stage=\"" << name
                   << "\"} "
                   << static_cast<double>(metrics.upload_complete_duration_microseconds[stage]) /
                          1'000'000.0
                   << '\n';
            output << "disk_upload_complete_stage_duration_seconds_count{stage=\"" << name
                   << "\"} " << metrics.upload_complete_duration_count[stage] << '\n';
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
        WriteMetricHeader(output, "disk_upload_tasks_active", "Upload tasks currently accepting chunks or finalizing.", "gauge");
        output << "disk_upload_tasks_active "
               << database.upload_tasks[0] + database.upload_tasks[4] << '\n';
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
        WriteMetricHeader(output, "disk_worker_claiming_enabled", "Whether this process is configured to claim storage jobs.", "gauge");
        output << "disk_worker_claiming_enabled "
               << (runtime_state.IsWorkerClaimingEnabled() ? 1 : 0) << '\n';
        WriteMetricHeader(output, "disk_worker_accepting_jobs", "Whether this process currently accepts storage jobs.", "gauge");
        output << "disk_worker_accepting_jobs "
               << (runtime_state.IsWorkerAccepting() ? 1 : 0) << '\n';
        WriteMetricHeader(output, "disk_process_business_requests_inflight", "In-flight business requests.", "gauge");
        output << "disk_process_business_requests_inflight "
               << runtime_state.BusinessRequestsInflight() << '\n';
        return output.str();
    }

} // namespace disk::metrics
