/**
 * @file StorageJobAdminDto.hpp
 * @brief Administrator contracts for persistent storage jobs
 */

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <drogon/HttpRequest.h>
#include <json/json.h>

#include "services/StorageJobRepository.hpp"
#include "utils/DtoBase.hpp"
#include "utils/ErrorCode.hpp"

namespace disk::admin {

    struct StorageJobListRequest final : DtoBase<StorageJobListRequest> {
        disk::jobs::StorageJobStatus status{ disk::jobs::StorageJobStatus::DeadLetter };
        std::optional<std::string> job_type;
        int page{ 1 };
        int page_size{ 20 };

        [[nodiscard]]
        static auto FromRequest(const drogon::HttpRequestPtr& request)
            -> Result<StorageJobListRequest> {
            StorageJobListRequest result;

            const auto status = request->getParameter("status");
            if (!status.empty()) {
                auto parsed = disk::jobs::ParseStorageJobStatus(status);
                if (!parsed.has_value()) {
                    return std::unexpected(ErrorInfo(
                        ErrorCode::ValidationFailed,
                        "Invalid storage job status"
                    ));
                }
                result.status = parsed.value();
            }

            const auto job_type = request->getParameter("job_type");
            if (!job_type.empty()) {
                if (!disk::jobs::IsKnownStorageJobType(job_type)) {
                    return std::unexpected(ErrorInfo(
                        ErrorCode::ValidationFailed,
                        "Invalid storage job type"
                    ));
                }
                result.job_type = job_type;
            }

            auto page = QueryPositiveInt(request, "page", 1);
            if (!page) {
                return std::unexpected(page.error());
            }
            if (page->has_value()) {
                result.page = page->value();
            }

            auto page_size = QueryPositiveInt(request, "page_size", 1, 100);
            if (!page_size) {
                return std::unexpected(page_size.error());
            }
            if (page_size->has_value()) {
                result.page_size = page_size->value();
            }
            return result;
        }
    };

    struct StorageJobReplayRequest final : DtoBase<StorageJobReplayRequest> {
        bool dry_run{ true };
        std::optional<uint64_t> confirm_job_id;
        std::string reason;

        [[nodiscard]]
        static auto ParseJobId(std::string_view value) -> Result<uint64_t> {
            return ParsePositiveUInt64(std::string(value), "id");
        }

        [[nodiscard]]
        static auto FromRequest(
            const drogon::HttpRequestPtr& request,
            uint64_t path_job_id
        ) -> Result<StorageJobReplayRequest> {
            auto body = RequireJsonBody(request);
            if (!body) {
                return std::unexpected(body.error());
            }
            const auto& json = **body;
            if (!json.isObject()) {
                return std::unexpected(ErrorInfo(
                    ErrorCode::ValidationFailed,
                    "Storage job replay body must be an object"
                ));
            }
            for (const auto& name : json.getMemberNames()) {
                if (name != "dry_run" && name != "confirm_job_id" && name != "reason") {
                    return std::unexpected(ErrorInfo(
                        ErrorCode::ValidationFailed,
                        "Storage job replay body contains an unknown field"
                    ));
                }
            }

            StorageJobReplayRequest result;
            auto dry_run = OptionalBool(json, "dry_run");
            if (!dry_run) {
                return std::unexpected(dry_run.error());
            }
            if (dry_run->has_value()) {
                result.dry_run = dry_run->value();
            }

            auto confirm_job_id = OptionalUInt64(json, "confirm_job_id");
            if (!confirm_job_id) {
                return std::unexpected(confirm_job_id.error());
            }
            result.confirm_job_id = confirm_job_id.value();
            if (result.confirm_job_id.has_value() &&
                (result.confirm_job_id.value() == 0 ||
                 result.confirm_job_id.value() != path_job_id)) {
                return std::unexpected(ErrorInfo(
                    ErrorCode::ValidationFailed,
                    "confirm_job_id must match the path job ID"
                ));
            }

            auto reason = OptionalString(json, "reason");
            if (!reason) {
                return std::unexpected(reason.error());
            }
            if (reason->has_value()) {
                result.reason = TrimWhitespace(reason->value());
                if (result.reason.size() > 256) {
                    return std::unexpected(ErrorInfo(
                        ErrorCode::ValidationFailed,
                        "Storage job replay reason must not exceed 256 characters"
                    ));
                }
            }

            if (!result.dry_run && !result.confirm_job_id.has_value()) {
                return std::unexpected(ErrorInfo(
                    ErrorCode::ValidationFailed,
                    "confirm_job_id is required when dry_run is false"
                ));
            }
            if (!result.dry_run && result.reason.empty()) {
                return std::unexpected(ErrorInfo(
                    ErrorCode::ValidationFailed,
                    "reason is required when dry_run is false"
                ));
            }
            return result;
        }
    };

    struct StorageJobItem final {
        uint64_t id{ 0 };
        std::string job_type;
        std::string aggregate_id;
        std::string dedupe_key;
        Json::Value payload{ Json::objectValue };
        disk::jobs::StorageJobStatus status{ disk::jobs::StorageJobStatus::Pending };
        uint32_t attempts{ 0 };
        uint32_t max_attempts{ 0 };
        std::string available_at;
        std::optional<std::string> locked_by;
        std::optional<std::string> locked_until;
        std::optional<std::string> last_error;
        std::string created_at;
        std::string updated_at;
        std::optional<std::string> completed_at;

        [[nodiscard]]
        auto ToJson(bool include_payload = false) const -> Json::Value {
            Json::Value json(Json::objectValue);
            json["id"] = Json::UInt64(id);
            json["job_type"] = job_type;
            json["aggregate_id"] = aggregate_id;
            json["dedupe_key"] = dedupe_key;
            json["status"] = std::string(disk::jobs::StorageJobStatusName(status));
            json["attempts"] = attempts;
            json["max_attempts"] = max_attempts;
            json["available_at"] = available_at;
            json["locked_by"] = locked_by.has_value() ? Json::Value(*locked_by) : Json::Value();
            json["locked_until"] = locked_until.has_value() ? Json::Value(*locked_until) : Json::Value();
            json["last_error"] = last_error.has_value() ? Json::Value(*last_error) : Json::Value();
            json["created_at"] = created_at;
            json["updated_at"] = updated_at;
            json["completed_at"] = completed_at.has_value() ? Json::Value(*completed_at) : Json::Value();
            if (include_payload) {
                json["payload"] = payload;
            }
            return json;
        }
    };

    struct StorageJobListResponse final {
        std::vector<StorageJobItem> items;
        int page{ 1 };
        int page_size{ 20 };
        uint64_t total{ 0 };
        uint64_t total_pages{ 0 };

        [[nodiscard]]
        auto ToJson() const -> Json::Value {
            Json::Value json(Json::objectValue);
            Json::Value values(Json::arrayValue);
            for (const auto& item : items) {
                values.append(item.ToJson());
            }
            json["items"] = std::move(values);
            Json::Value pagination(Json::objectValue);
            pagination["page"] = page;
            pagination["page_size"] = page_size;
            pagination["total"] = Json::UInt64(total);
            pagination["total_pages"] = Json::UInt64(total_pages);
            json["pagination"] = std::move(pagination);
            return json;
        }
    };

    struct StorageJobReplayResponse final {
        StorageJobItem job;
        bool dry_run{ true };
        bool eligible{ false };
        bool replayed{ false };

        [[nodiscard]]
        auto ToJson() const -> Json::Value {
            Json::Value json(Json::objectValue);
            json["job"] = job.ToJson(true);
            json["dry_run"] = dry_run;
            json["eligible"] = eligible;
            json["replayed"] = replayed;
            return json;
        }
    };

} // namespace disk::admin
