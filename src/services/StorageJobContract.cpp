/**
 * @file StorageJobContract.cpp
 * @brief 周期存储任务合同实现
 */

#include "services/StorageJobContract.hpp"

#include <algorithm>
#include <cctype>
#include <limits>
#include <string_view>
#include <utility>

#include "utils/FileHashUtil.hpp"

namespace disk::jobs {
    namespace {
        [[nodiscard]] auto IsSafeScanId(std::string_view scan_id) -> bool {
            return !scan_id.empty() && scan_id.size() <= 64 &&
                   std::ranges::all_of(scan_id, [](unsigned char character) {
                       return std::isalnum(character) != 0 || character == '.' ||
                              character == '_' || character == ':' || character == '-';
                   });
        }

        [[nodiscard]] auto BuildReconciliationCursorDigest(
            const disk::reconciliation::ReconciliationPageRequest& request
        ) -> std::string {
            return disk::utils::FileHashUtil::HashSha256(
                std::string(disk::reconciliation::ToStorageValue(request.scope)) + "\n" +
                std::to_string(request.after_id) + "\n" + request.continuation_token
            );
        }

        [[nodiscard]] auto ValidateObjectShape(
            const Json::Value& payload,
            Json::ArrayIndex expected_fields,
            std::string_view job_type
        ) -> std::expected<void, std::string> {
            if (!payload.isObject() || payload.size() != expected_fields) {
                return std::unexpected(
                    std::string(job_type) + " payload has an invalid object shape"
                );
            }
            return {};
        }
    } // namespace

    auto ValidateExpireUploadsPageRequest(const ExpireUploadsPageRequest& request)
        -> std::expected<void, std::string> {
        if (!IsSafeScanId(request.scan_id)) {
            return std::unexpected("expire_uploads scan_id is invalid");
        }
        if (request.page == std::numeric_limits<uint64_t>::max()) {
            return std::unexpected("expire_uploads page cannot be incremented");
        }
        if (request.limit == 0 || request.limit > kMaxExpireUploadsPageSize) {
            return std::unexpected("expire_uploads limit must be in range 1-500");
        }
        return {};
    }

    auto BuildExpireUploadsJob(const ExpireUploadsPageRequest& request)
        -> std::expected<NewStorageJob, std::string> {
        auto validation = ValidateExpireUploadsPageRequest(request);
        if (!validation) {
            return std::unexpected(validation.error());
        }

        Json::Value payload(Json::objectValue);
        payload["scan_id"] = request.scan_id;
        payload["page"] = Json::UInt64(request.page);
        payload["limit"] = Json::UInt64(request.limit);
        return NewStorageJob{
            .job_type = std::string(kExpireUploadsJobType),
            .aggregate_id = request.scan_id,
            .dedupe_key = "periodic:expire-uploads:" + request.scan_id + ":" +
                          std::to_string(request.page),
            .payload = std::move(payload),
        };
    }

    auto ParseExpireUploadsJob(const StorageJob& job)
        -> std::expected<ExpireUploadsPageRequest, std::string> {
        if (job.job_type != kExpireUploadsJobType) {
            return std::unexpected("expire_uploads job type does not match parser");
        }
        auto shape = ValidateObjectShape(job.payload, 3, kExpireUploadsJobType);
        if (!shape) {
            return std::unexpected(shape.error());
        }
        if (!job.payload["scan_id"].isString() || !job.payload["page"].isUInt64() ||
            !job.payload["limit"].isUInt64() ||
            job.payload["limit"].asUInt64() > std::numeric_limits<size_t>::max()) {
            return std::unexpected("expire_uploads payload field types are invalid");
        }

        ExpireUploadsPageRequest request{
            .scan_id = job.payload["scan_id"].asString(),
            .page = job.payload["page"].asUInt64(),
            .limit = static_cast<size_t>(job.payload["limit"].asUInt64()),
        };
        auto expected_job = BuildExpireUploadsJob(request);
        if (!expected_job) {
            return std::unexpected(expected_job.error());
        }
        if (job.aggregate_id != expected_job->aggregate_id) {
            return std::unexpected("expire_uploads aggregate_id does not match scan_id");
        }
        if (job.dedupe_key != expected_job->dedupe_key) {
            return std::unexpected("expire_uploads dedupe_key does not match payload");
        }
        return request;
    }

    auto BuildStorageReconcileJob(
        const disk::reconciliation::ReconciliationPageRequest& request
    ) -> std::expected<NewStorageJob, std::string> {
        auto validation = disk::reconciliation::ValidateReconciliationPageRequest(request);
        if (!validation) {
            return std::unexpected(validation.error().message);
        }

        const auto scope = std::string(disk::reconciliation::ToStorageValue(request.scope));
        Json::Value payload(Json::objectValue);
        payload["scan_id"] = request.scan_id;
        payload["scope"] = scope;
        payload["after_id"] = Json::UInt64(request.after_id);
        payload["continuation_token"] = request.continuation_token;
        payload["limit"] = Json::UInt64(request.limit);
        return NewStorageJob{
            .job_type = std::string(kStorageReconcileJobType),
            .aggregate_id = request.scan_id,
            .dedupe_key = "periodic:storage-reconcile:" + request.scan_id + ":" + scope + ":" +
                          BuildReconciliationCursorDigest(request),
            .payload = std::move(payload),
        };
    }

    auto ParseStorageReconcileJob(const StorageJob& job)
        -> std::expected<disk::reconciliation::ReconciliationPageRequest, std::string> {
        if (job.job_type != kStorageReconcileJobType) {
            return std::unexpected("storage_reconcile job type does not match parser");
        }
        auto shape = ValidateObjectShape(job.payload, 5, kStorageReconcileJobType);
        if (!shape) {
            return std::unexpected(shape.error());
        }
        if (!job.payload["scan_id"].isString() || !job.payload["scope"].isString() ||
            !job.payload["after_id"].isUInt64() ||
            !job.payload["continuation_token"].isString() ||
            !job.payload["limit"].isUInt64() ||
            job.payload["limit"].asUInt64() > std::numeric_limits<size_t>::max()) {
            return std::unexpected("storage_reconcile payload field types are invalid");
        }

        auto scope = disk::reconciliation::ParseReconciliationScope(
            job.payload["scope"].asString()
        );
        if (!scope.has_value()) {
            return std::unexpected("storage_reconcile scope is invalid");
        }
        disk::reconciliation::ReconciliationPageRequest request{
            .scan_id = job.payload["scan_id"].asString(),
            .scope = scope.value(),
            .after_id = job.payload["after_id"].asUInt64(),
            .continuation_token = job.payload["continuation_token"].asString(),
            .limit = static_cast<size_t>(job.payload["limit"].asUInt64()),
        };
        auto expected_job = BuildStorageReconcileJob(request);
        if (!expected_job) {
            return std::unexpected(expected_job.error());
        }
        if (job.aggregate_id != expected_job->aggregate_id) {
            return std::unexpected("storage_reconcile aggregate_id does not match scan_id");
        }
        if (job.dedupe_key != expected_job->dedupe_key) {
            return std::unexpected("storage_reconcile dedupe_key does not match payload");
        }
        return request;
    }

} // namespace disk::jobs
