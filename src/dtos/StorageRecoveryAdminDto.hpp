/**
 * @file StorageRecoveryAdminDto.hpp
 * @brief Administrator contracts for audited storage recovery commands
 */

#pragma once

#include <algorithm>
#include <cstdint>
#include <initializer_list>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include <drogon/HttpRequest.h>
#include <json/json.h>

#include "services/StorageJobRepository.hpp"
#include "services/StorageReconciliationService.hpp"
#include "services/UploadStateMachine.hpp"
#include "utils/DtoBase.hpp"
#include "utils/ErrorCode.hpp"

namespace disk::admin {
    namespace recovery_dto_detail {
        struct ConfirmedCommandFields final {
            bool dry_run{ true };
            std::optional<std::string> confirmation;
            std::optional<uint64_t> expected_state_version;
            std::string reason;
        };

        class Parser final : private DtoBase<Parser> {
        public:
            [[nodiscard]]
            static auto Body(const drogon::HttpRequestPtr& request)
                -> Result<std::shared_ptr<const Json::Value>> {
                auto body = RequireJsonBody(request);
                if (!body) {
                    return std::unexpected(body.error());
                }
                if (!(**body).isObject()) {
                    return std::unexpected(ErrorInfo(
                        ErrorCode::ValidationFailed,
                        "Recovery command body must be an object"
                    ));
                }
                return body.value();
            }

            [[nodiscard]]
            static auto SafeIdentifier(std::string_view value, std::string_view name)
                -> Result<void> {
                if (value.empty() || value.size() > 64 ||
                    !std::ranges::all_of(value, [](char character) {
                        return (character >= 'a' && character <= 'z') ||
                               (character >= 'A' && character <= 'Z') ||
                               (character >= '0' && character <= '9') ||
                               character == '.' || character == '_' ||
                               character == ':' || character == '-';
                    })) {
                    return std::unexpected(ErrorInfo(
                        ErrorCode::ValidationFailed,
                        "Invalid " + std::string(name)
                    ));
                }
                return {};
            }

            [[nodiscard]]
            static auto KnownFields(
                const Json::Value& json,
                std::initializer_list<std::string_view> allowed
            ) -> Result<void> {
                for (const auto& name : json.getMemberNames()) {
                    if (std::ranges::find(allowed, name) == allowed.end()) {
                        return std::unexpected(ErrorInfo(
                            ErrorCode::ValidationFailed,
                            "Recovery command body contains an unknown field"
                        ));
                    }
                }
                return {};
            }

            [[nodiscard]]
            static auto ConfirmedFields(
                const Json::Value& json,
                std::string_view path_id,
                const char* confirmation_field,
                bool uses_state_version
            ) -> Result<ConfirmedCommandFields> {
                ConfirmedCommandFields result;

                auto dry_run = OptionalBool(json, "dry_run");
                if (!dry_run) {
                    return std::unexpected(dry_run.error());
                }
                result.dry_run = dry_run->value_or(true);

                auto confirmation = OptionalString(json, confirmation_field);
                if (!confirmation) {
                    return std::unexpected(confirmation.error());
                }
                result.confirmation = std::move(confirmation.value());
                if (result.confirmation.has_value()) {
                    auto validation = SafeIdentifier(*result.confirmation, confirmation_field);
                    if (!validation) {
                        return std::unexpected(validation.error());
                    }
                    if (*result.confirmation != path_id) {
                        return std::unexpected(ErrorInfo(
                            ErrorCode::ValidationFailed,
                            std::string(confirmation_field) + " must match the path ID"
                        ));
                    }
                }

                if (uses_state_version) {
                    auto expected_state_version = OptionalUInt64(
                        json,
                        "expected_state_version"
                    );
                    if (!expected_state_version) {
                        return std::unexpected(expected_state_version.error());
                    }
                    result.expected_state_version = expected_state_version.value();
                    if (result.expected_state_version == 0) {
                        return std::unexpected(ErrorInfo(
                            ErrorCode::ValidationFailed,
                            "expected_state_version must be positive"
                        ));
                    }
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
                            "Recovery command reason must not exceed 256 characters"
                        ));
                    }
                }

                if (!result.dry_run && !result.confirmation.has_value()) {
                    return std::unexpected(ErrorInfo(
                        ErrorCode::ValidationFailed,
                        std::string(confirmation_field) + " is required when dry_run is false"
                    ));
                }
                if (!result.dry_run && uses_state_version &&
                    !result.expected_state_version.has_value()) {
                    return std::unexpected(ErrorInfo(
                        ErrorCode::ValidationFailed,
                        "expected_state_version is required when dry_run is false"
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

            [[nodiscard]]
            static auto OptionalText(const Json::Value& json, const char* field)
                -> Result<std::optional<std::string>> {
                return OptionalString(json, field);
            }

            [[nodiscard]]
            static auto RequiredText(const Json::Value& json, const char* field)
                -> Result<std::string> {
                return RequireString(json, field);
            }
        };
    } // namespace recovery_dto_detail

    struct UploadLeaseReleaseRequest final {
        std::string upload_id;
        bool dry_run{ true };
        std::optional<std::string> confirm_upload_id;
        std::optional<uint64_t> expected_state_version;
        std::optional<std::string> expected_lease_owner;
        std::string reason;

        [[nodiscard]]
        static auto FromRequest(
            const drogon::HttpRequestPtr& request,
            std::string_view path_upload_id
        ) -> Result<UploadLeaseReleaseRequest> {
            auto id_validation = recovery_dto_detail::Parser::SafeIdentifier(
                path_upload_id,
                "upload ID"
            );
            if (!id_validation) {
                return std::unexpected(id_validation.error());
            }
            auto body = recovery_dto_detail::Parser::Body(request);
            if (!body) {
                return std::unexpected(body.error());
            }
            auto fields = recovery_dto_detail::Parser::KnownFields(
                **body,
                {
                    "dry_run",
                    "confirm_upload_id",
                    "expected_state_version",
                    "expected_lease_owner",
                    "reason",
                }
            );
            if (!fields) {
                return std::unexpected(fields.error());
            }
            auto common = recovery_dto_detail::Parser::ConfirmedFields(
                **body,
                path_upload_id,
                "confirm_upload_id",
                true
            );
            if (!common) {
                return std::unexpected(common.error());
            }
            auto owner = recovery_dto_detail::Parser::OptionalText(
                **body,
                "expected_lease_owner"
            );
            if (!owner) {
                return std::unexpected(owner.error());
            }
            if (owner->has_value() && (owner->value().empty() || owner->value().size() > 128)) {
                return std::unexpected(ErrorInfo(
                    ErrorCode::ValidationFailed,
                    "expected_lease_owner must contain 1 to 128 characters"
                ));
            }
            if (!common->dry_run && !owner->has_value()) {
                return std::unexpected(ErrorInfo(
                    ErrorCode::ValidationFailed,
                    "expected_lease_owner is required when dry_run is false"
                ));
            }

            return UploadLeaseReleaseRequest{
                .upload_id = std::string(path_upload_id),
                .dry_run = common->dry_run,
                .confirm_upload_id = std::move(common->confirmation),
                .expected_state_version = common->expected_state_version,
                .expected_lease_owner = std::move(owner.value()),
                .reason = std::move(common->reason),
            };
        }
    };

    struct UploadCleanupRebuildRequest final {
        std::string upload_id;
        bool dry_run{ true };
        std::optional<std::string> confirm_upload_id;
        std::optional<uint64_t> expected_state_version;
        std::string reason;

        [[nodiscard]]
        static auto FromRequest(
            const drogon::HttpRequestPtr& request,
            std::string_view path_upload_id
        ) -> Result<UploadCleanupRebuildRequest> {
            auto id_validation = recovery_dto_detail::Parser::SafeIdentifier(
                path_upload_id,
                "upload ID"
            );
            if (!id_validation) {
                return std::unexpected(id_validation.error());
            }
            auto body = recovery_dto_detail::Parser::Body(request);
            if (!body) {
                return std::unexpected(body.error());
            }
            auto fields = recovery_dto_detail::Parser::KnownFields(
                **body,
                {
                    "dry_run",
                    "confirm_upload_id",
                    "expected_state_version",
                    "reason",
                }
            );
            if (!fields) {
                return std::unexpected(fields.error());
            }
            auto common = recovery_dto_detail::Parser::ConfirmedFields(
                **body,
                path_upload_id,
                "confirm_upload_id",
                true
            );
            if (!common) {
                return std::unexpected(common.error());
            }
            return UploadCleanupRebuildRequest{
                .upload_id = std::string(path_upload_id),
                .dry_run = common->dry_run,
                .confirm_upload_id = std::move(common->confirmation),
                .expected_state_version = common->expected_state_version,
                .reason = std::move(common->reason),
            };
        }
    };

    struct StorageReconciliationEnqueueRequest final {
        std::string scan_id;
        disk::reconciliation::ReconciliationScope scope{
            disk::reconciliation::ReconciliationScope::Contents
        };
        bool dry_run{ true };
        std::optional<std::string> confirm_scan_id;
        std::string reason;

        [[nodiscard]]
        static auto FromRequest(
            const drogon::HttpRequestPtr& request,
            std::string_view path_scan_id
        ) -> Result<StorageReconciliationEnqueueRequest> {
            auto id_validation = recovery_dto_detail::Parser::SafeIdentifier(
                path_scan_id,
                "scan ID"
            );
            if (!id_validation) {
                return std::unexpected(id_validation.error());
            }
            auto body = recovery_dto_detail::Parser::Body(request);
            if (!body) {
                return std::unexpected(body.error());
            }
            auto fields = recovery_dto_detail::Parser::KnownFields(
                **body,
                { "scope", "dry_run", "confirm_scan_id", "reason" }
            );
            if (!fields) {
                return std::unexpected(fields.error());
            }
            auto scope_value = recovery_dto_detail::Parser::RequiredText(**body, "scope");
            if (!scope_value) {
                return std::unexpected(scope_value.error());
            }
            auto scope = disk::reconciliation::ParseReconciliationScope(scope_value.value());
            if (!scope.has_value()) {
                return std::unexpected(ErrorInfo(
                    ErrorCode::ValidationFailed,
                    "Invalid reconciliation scope"
                ));
            }
            auto common = recovery_dto_detail::Parser::ConfirmedFields(
                **body,
                path_scan_id,
                "confirm_scan_id",
                false
            );
            if (!common) {
                return std::unexpected(common.error());
            }
            return StorageReconciliationEnqueueRequest{
                .scan_id = std::string(path_scan_id),
                .scope = scope.value(),
                .dry_run = common->dry_run,
                .confirm_scan_id = std::move(common->confirmation),
                .reason = std::move(common->reason),
            };
        }
    };

    struct UploadLeaseReleaseResponse final {
        std::string upload_id;
        bool dry_run{ true };
        bool eligible{ false };
        bool released{ false };
        disk::upload::UploadTaskStatus status{ disk::upload::UploadTaskStatus::InProgress };
        uint64_t state_version{ 0 };
        std::optional<std::string> lease_owner;
        std::optional<std::string> lease_expires_at;
        bool lease_expired{ false };

        [[nodiscard]] auto ToJson() const -> Json::Value {
            Json::Value json(Json::objectValue);
            json["upload_id"] = upload_id;
            json["dry_run"] = dry_run;
            json["eligible"] = eligible;
            json["released"] = released;
            json["status"] = std::string(disk::upload::UploadTaskStatusName(status));
            json["state_version"] = Json::UInt64(state_version);
            json["lease_owner"] = lease_owner.has_value() ?
                                      Json::Value(*lease_owner) :
                                      Json::Value();
            json["lease_expires_at"] = lease_expires_at.has_value() ?
                                           Json::Value(*lease_expires_at) :
                                           Json::Value();
            json["lease_expired"] = lease_expired;
            return json;
        }
    };

    struct UploadCleanupRebuildResponse final {
        std::string upload_id;
        bool dry_run{ true };
        bool eligible{ false };
        bool rebuilt{ false };
        disk::upload::UploadTaskStatus status{ disk::upload::UploadTaskStatus::InProgress };
        uint64_t state_version{ 0 };
        std::string planned_action{ "none" };
        std::optional<uint64_t> job_id;
        std::optional<disk::jobs::StorageJobStatus> job_status;

        [[nodiscard]] auto ToJson() const -> Json::Value {
            Json::Value json(Json::objectValue);
            json["upload_id"] = upload_id;
            json["dry_run"] = dry_run;
            json["eligible"] = eligible;
            json["rebuilt"] = rebuilt;
            json["status"] = std::string(disk::upload::UploadTaskStatusName(status));
            json["state_version"] = Json::UInt64(state_version);
            json["planned_action"] = planned_action;
            json["job_id"] = job_id.has_value() ?
                                 Json::Value(Json::UInt64(*job_id)) :
                                 Json::Value();
            json["job_status"] = job_status.has_value() ?
                                     Json::Value(std::string(disk::jobs::StorageJobStatusName(*job_status))) :
                                     Json::Value();
            return json;
        }
    };

    struct StorageReconciliationEnqueueResponse final {
        std::string scan_id;
        disk::reconciliation::ReconciliationScope scope{
            disk::reconciliation::ReconciliationScope::Contents
        };
        bool dry_run{ true };
        bool eligible{ false };
        bool enqueued{ false };
        uint64_t page_size{ 0 };
        std::string dedupe_key;
        std::optional<uint64_t> job_id;
        std::optional<disk::jobs::StorageJobStatus> job_status;

        [[nodiscard]] auto ToJson() const -> Json::Value {
            Json::Value json(Json::objectValue);
            json["scan_id"] = scan_id;
            json["scope"] = std::string(disk::reconciliation::ToStorageValue(scope));
            json["dry_run"] = dry_run;
            json["eligible"] = eligible;
            json["enqueued"] = enqueued;
            json["page_size"] = Json::UInt64(page_size);
            json["dedupe_key"] = dedupe_key;
            json["job_id"] = job_id.has_value() ?
                                 Json::Value(Json::UInt64(*job_id)) :
                                 Json::Value();
            json["job_status"] = job_status.has_value() ?
                                     Json::Value(std::string(disk::jobs::StorageJobStatusName(*job_status))) :
                                     Json::Value();
            return json;
        }
    };

} // namespace disk::admin
