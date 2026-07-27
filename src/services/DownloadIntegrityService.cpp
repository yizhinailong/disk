/**
 * @file DownloadIntegrityService.cpp
 * @brief 下载 Blob 完整性预检与对账记录服务实现
 */

#include "services/DownloadIntegrityService.hpp"

#include <string>
#include <string_view>
#include <utility>

#include <drogon/drogon.h>
#include <json/json.h>

#include "services/StorageReconciliationService.hpp"
#include "storage/IBlobStore.hpp"
#include "utils/LogHelper.hpp"

namespace disk::download {
    namespace {
        [[nodiscard]] auto MakeFinding(
            std::string_view finding_type,
            const disk::storage::BlobDescriptor& blob,
            Json::Value details
        ) -> disk::reconciliation::ReconciliationFinding {
            return disk::reconciliation::ReconciliationFinding{
                .finding_type = std::string(finding_type),
                .resource_id = std::to_string(blob.content_id),
                .resource_locator = blob.storage_path,
                .severity = disk::reconciliation::ReconciliationSeverity::Critical,
                .resolution_strategy = disk::reconciliation::ResolutionStrategy::Manual,
                .details = std::move(details),
            };
        }

        auto RecordFindingBestEffort(
            const std::shared_ptr<disk::reconciliation::StorageReconciliationService>& service,
            disk::reconciliation::ReconciliationFinding finding,
            disk::utils::LogContext log_context
        ) -> drogon::Task<void> {
            if (service == nullptr) {
                co_return;
            }

            const auto resource_id = finding.resource_id;
            const auto finding_type = finding.finding_type;
            try {
                co_await service->RecordFinding(finding);
            } catch (...) {
                Logger::Error(log_context)
                    << "Download reconciliation finding write failed: finding_type="
                    << finding_type << ", content_id=" << resource_id;
            }
        }

        auto AddCorrelationDetails(
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

        [[nodiscard]] auto FileReadFailure() -> Result<void> {
            return std::unexpected(ErrorInfo(ErrorCode::FileReadError));
        }
    } // namespace

    DownloadIntegrityService::DownloadIntegrityService(
        drogon::orm::DbClientPtr db_client,
        disk::storage::IBlobStore* blob_store
    ) : m_blob_store(blob_store),
        m_reconciliation_service(
            std::make_shared<disk::reconciliation::StorageReconciliationService>(
                std::move(db_client),
                nullptr,
                blob_store
            )
        ) {
    }

    auto DownloadIntegrityService::Preflight(
        const disk::storage::BlobDescriptor& blob,
        uint64_t expected_size,
        disk::utils::LogContext log_context
    ) -> drogon::Task<Result<void>> {
        if (m_blob_store == nullptr) {
            Logger::Error(log_context) << "Download BlobStore is unavailable";
            co_return FileReadFailure();
        }

        try {
            auto observed_size = co_await m_blob_store->GetBlobSize(blob, log_context);
            if (!observed_size) {
                if (observed_size.error().code == ErrorCode::FileNotFound) {
                    Json::Value details(Json::objectValue);
                    details["source"] = "download_preflight";
                    details["expected_size"] = Json::UInt64(expected_size);
                    AddCorrelationDetails(details, log_context);
                    co_await RecordFindingBestEffort(
                        m_reconciliation_service,
                        MakeFinding(
                            disk::reconciliation::kMissingFinalBlobFindingType,
                            blob,
                            std::move(details)
                        ),
                        log_context
                    );
                }
                co_return FileReadFailure();
            }

            if (*observed_size != expected_size) {
                Json::Value details(Json::objectValue);
                details["source"] = "download_preflight";
                details["expected_size"] = Json::UInt64(expected_size);
                details["observed_size"] = Json::UInt64(*observed_size);
                AddCorrelationDetails(details, log_context);
                co_await RecordFindingBestEffort(
                    m_reconciliation_service,
                    MakeFinding(
                        disk::reconciliation::kFinalBlobSizeMismatchFindingType,
                        blob,
                        std::move(details)
                    ),
                    log_context
                );
                co_return FileReadFailure();
            }
        } catch (...) {
            Logger::Error(log_context)
                << "Download Blob preflight failed: content_id=" << blob.content_id;
            co_return FileReadFailure();
        }

        co_return Result<void>{};
    }

    auto DownloadIntegrityService::RecordOpenFailure(
        const disk::storage::BlobDescriptor& blob,
        ErrorCode error_code,
        uint64_t range_start,
        uint64_t expected_bytes,
        disk::utils::LogContext log_context
    ) -> drogon::Task<void> {
        Json::Value details(Json::objectValue);
        details["source"] = "download_open";
        details["range_start"] = Json::UInt64(range_start);
        details["expected_bytes"] = Json::UInt64(expected_bytes);
        AddCorrelationDetails(details, log_context);

        const auto finding_type = error_code == ErrorCode::FileNotFound ?
                                      disk::reconciliation::kMissingFinalBlobFindingType :
                                      disk::reconciliation::kFinalBlobReadInterruptedFindingType;
        co_await RecordFindingBestEffort(
            m_reconciliation_service,
            MakeFinding(finding_type, blob, std::move(details)),
            log_context
        );
    }

    auto DownloadIntegrityService::RecordStreamInterruption(
        const disk::storage::BlobDescriptor& blob,
        uint64_t range_start,
        uint64_t expected_bytes,
        uint64_t delivered_bytes,
        disk::utils::LogContext log_context
    ) noexcept -> void {
        try {
            Json::Value details(Json::objectValue);
            details["source"] = "download_stream";
            details["range_start"] = Json::UInt64(range_start);
            details["expected_bytes"] = Json::UInt64(expected_bytes);
            details["delivered_bytes"] = Json::UInt64(delivered_bytes);
            AddCorrelationDetails(details, log_context);

            auto finding = MakeFinding(
                disk::reconciliation::kFinalBlobReadInterruptedFindingType,
                blob,
                std::move(details)
            );
            const auto service = m_reconciliation_service;
            drogon::async_run(
                [service, finding = std::move(finding), log_context]() mutable
                    -> drogon::Task<void> {
                    co_await RecordFindingBestEffort(
                        service,
                        std::move(finding),
                        std::move(log_context)
                    );
                }
            );
        } catch (...) {
            Logger::Error(log_context)
                << "Download stream reconciliation scheduling failed: content_id="
                << blob.content_id;
        }
    }

} // namespace disk::download
