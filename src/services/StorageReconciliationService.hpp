/**
 * @file StorageReconciliationService.hpp
 * @brief 存储、引用和配额一致性巡检服务
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include <drogon/orm/DbClient.h>
#include <json/json.h>

#include "utils/ErrorCode.hpp"

namespace disk::storage {
    class IBlobStore;
    class UploadStagingStorage;
} // namespace disk::storage

namespace disk::reconciliation {

    inline constexpr size_t kMaxDatabaseReconciliationPageSize = 500;
    inline constexpr size_t kMaxObjectReconciliationPageSize = 1000;
    inline constexpr std::string_view kMissingFinalBlobFindingType = "missing_final_blob";
    inline constexpr std::string_view kFinalBlobSizeMismatchFindingType =
        "final_blob_size_mismatch";
    inline constexpr std::string_view kFinalBlobReadInterruptedFindingType =
        "final_blob_read_interrupted";
    inline constexpr std::string_view kUploadStagingMismatchFindingType =
        "upload_staging_mismatch";

    enum class ReconciliationScope {
        Contents,
        Users,
        Staging,
        Final,
    };

    [[nodiscard]] constexpr auto ToStorageValue(ReconciliationScope scope) noexcept
        -> std::string_view {
        switch (scope) {
            case ReconciliationScope::Contents:
                return "contents";
            case ReconciliationScope::Users:
                return "users";
            case ReconciliationScope::Staging:
                return "staging";
            case ReconciliationScope::Final:
                return "final";
        }
        return "contents";
    }

    [[nodiscard]] constexpr auto ParseReconciliationScope(std::string_view value) noexcept
        -> std::optional<ReconciliationScope> {
        if (value == "contents") {
            return ReconciliationScope::Contents;
        }
        if (value == "users") {
            return ReconciliationScope::Users;
        }
        if (value == "staging") {
            return ReconciliationScope::Staging;
        }
        if (value == "final") {
            return ReconciliationScope::Final;
        }
        return std::nullopt;
    }

    enum class ReconciliationSeverity : int16_t {
        Info = 0,
        Warning = 1,
        Critical = 2,
    };

    enum class ResolutionStrategy {
        AutoGc,
        Alert,
        Manual,
    };

    [[nodiscard]] constexpr auto ToStorageValue(ResolutionStrategy strategy) noexcept
        -> std::string_view {
        switch (strategy) {
            case ResolutionStrategy::AutoGc:
                return "auto_gc";
            case ResolutionStrategy::Alert:
                return "alert";
            case ResolutionStrategy::Manual:
                return "manual";
        }
        return "manual";
    }

    struct ReconciliationPageRequest {
        std::string scan_id;
        ReconciliationScope scope{ ReconciliationScope::Contents };
        uint64_t after_id{ 0 };
        std::string continuation_token;
        size_t limit{ 100 };
    };

    struct ReconciliationPageResult {
        size_t inspected{ 0 };
        size_t findings_recorded{ 0 };
        size_t repairs_enqueued{ 0 };
        uint64_t next_after_id{ 0 };
        std::string next_continuation_token;
        bool has_more{ false };
    };

    struct ReconciliationFinding {
        std::string finding_type;
        std::string resource_id;
        std::optional<std::string> resource_locator;
        ReconciliationSeverity severity{ ReconciliationSeverity::Warning };
        ResolutionStrategy resolution_strategy{ ResolutionStrategy::Alert };
        Json::Value details{ Json::objectValue };
    };

    [[nodiscard]] auto ValidateReconciliationPageRequest(
        const ReconciliationPageRequest& request
    ) -> Result<void>;

    [[nodiscard]] auto BuildObjectResourceId(std::string_view locator) -> std::string;

    [[nodiscard]] constexpr auto ShouldEnqueueBlobGc(
        int64_t stored_ref_count,
        uint64_t actual_ref_count
    ) noexcept -> bool {
        return stored_ref_count == 0 && actual_ref_count == 0;
    }

    class StorageReconciliationService {
    public:
        StorageReconciliationService(
            drogon::orm::DbClientPtr db_client,
            disk::storage::UploadStagingStorage* staging_storage,
            disk::storage::IBlobStore* blob_store
        );

        [[nodiscard]]
        auto RunPage(const ReconciliationPageRequest& request) const
            -> drogon::Task<Result<ReconciliationPageResult>>;

        auto RecordFinding(const ReconciliationFinding& finding) const -> drogon::Task<void>;

    private:
        [[nodiscard]]
        auto RunContentPage(const ReconciliationPageRequest& request) const
            -> drogon::Task<Result<ReconciliationPageResult>>;

        [[nodiscard]]
        auto RunUserPage(const ReconciliationPageRequest& request) const
            -> drogon::Task<Result<ReconciliationPageResult>>;

        [[nodiscard]]
        auto RunObjectPage(const ReconciliationPageRequest& request) const
            -> drogon::Task<Result<ReconciliationPageResult>>;

        auto ResolveFinding(std::string_view finding_type, std::string_view resource_id) const
            -> drogon::Task<void>;

        auto ResolveStaleObjectFindings(
            std::string_view finding_type,
            const ReconciliationPageRequest& request
        ) const -> drogon::Task<void>;

        drogon::orm::DbClientPtr m_db_client;
        disk::storage::UploadStagingStorage* m_staging_storage{};
        disk::storage::IBlobStore* m_blob_store{};
    };

} // namespace disk::reconciliation
