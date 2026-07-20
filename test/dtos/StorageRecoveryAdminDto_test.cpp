#include "dtos/StorageRecoveryAdminDto.hpp"

#include <array>
#include <string>
#include <utility>

#include <drogon/HttpRequest.h>
#include <gtest/gtest.h>
#include <json/json.h>

namespace disk::admin {
    namespace {
        auto JsonRequest(const Json::Value& value) -> drogon::HttpRequestPtr {
            Json::StreamWriterBuilder builder;
            builder["indentation"] = "";
            auto request = drogon::HttpRequest::newHttpRequest();
            request->setBody(Json::writeString(builder, value));
            request->setContentTypeCode(drogon::CT_APPLICATION_JSON);
            return request;
        }

        TEST(UploadLeaseReleaseRequestTest, DefaultsToDryRunWithoutConfirmation) {
            auto parsed = UploadLeaseReleaseRequest::FromRequest(
                JsonRequest(Json::Value(Json::objectValue)),
                "upload-123"
            );

            ASSERT_TRUE(parsed.has_value());
            EXPECT_EQ(parsed->upload_id, "upload-123");
            EXPECT_TRUE(parsed->dry_run);
            EXPECT_FALSE(parsed->confirm_upload_id.has_value());
            EXPECT_FALSE(parsed->expected_state_version.has_value());
            EXPECT_FALSE(parsed->expected_lease_owner.has_value());
            EXPECT_TRUE(parsed->reason.empty());
        }

        TEST(UploadLeaseReleaseRequestTest, ParsesExactActualConfirmation) {
            Json::Value body(Json::objectValue);
            body["dry_run"] = false;
            body["confirm_upload_id"] = "upload-123";
            body["expected_state_version"] = Json::UInt64(7);
            body["expected_lease_owner"] = "api-a:complete:123";
            body["reason"] = "  owner terminated  ";

            auto parsed = UploadLeaseReleaseRequest::FromRequest(
                JsonRequest(body),
                "upload-123"
            );

            ASSERT_TRUE(parsed.has_value());
            EXPECT_FALSE(parsed->dry_run);
            EXPECT_EQ(parsed->confirm_upload_id, "upload-123");
            EXPECT_EQ(parsed->expected_state_version, 7U);
            EXPECT_EQ(parsed->expected_lease_owner, "api-a:complete:123");
            EXPECT_EQ(parsed->reason, "owner terminated");
        }

        TEST(UploadLeaseReleaseRequestTest, RejectsUnsafeActualContracts) {
            Json::Value missing_owner(Json::objectValue);
            missing_owner["dry_run"] = false;
            missing_owner["confirm_upload_id"] = "upload-123";
            missing_owner["expected_state_version"] = Json::UInt64(7);
            missing_owner["reason"] = "release";
            EXPECT_FALSE(
                UploadLeaseReleaseRequest::FromRequest(
                    JsonRequest(missing_owner),
                    "upload-123"
                )
                    .has_value()
            );

            Json::Value zero_version = missing_owner;
            zero_version["expected_lease_owner"] = "api-a";
            zero_version["expected_state_version"] = Json::UInt64(0);
            EXPECT_FALSE(
                UploadLeaseReleaseRequest::FromRequest(
                    JsonRequest(zero_version),
                    "upload-123"
                )
                    .has_value()
            );

            Json::Value mismatched = zero_version;
            mismatched["expected_state_version"] = Json::UInt64(7);
            mismatched["confirm_upload_id"] = "upload-456";
            EXPECT_FALSE(
                UploadLeaseReleaseRequest::FromRequest(
                    JsonRequest(mismatched),
                    "upload-123"
                )
                    .has_value()
            );

            Json::Value unknown(Json::objectValue);
            unknown["force"] = true;
            EXPECT_FALSE(
                UploadLeaseReleaseRequest::FromRequest(
                    JsonRequest(unknown),
                    "upload-123"
                )
                    .has_value()
            );
            EXPECT_FALSE(
                UploadLeaseReleaseRequest::FromRequest(
                    JsonRequest(Json::Value(Json::objectValue)),
                    "../upload"
                )
                    .has_value()
            );
        }

        TEST(UploadCleanupRebuildRequestTest, RequiresVersionForActualRebuild) {
            Json::Value body(Json::objectValue);
            body["dry_run"] = false;
            body["confirm_upload_id"] = "upload-123";
            body["expected_state_version"] = Json::UInt64(8);
            body["reason"] = "  objects remain  ";

            auto parsed = UploadCleanupRebuildRequest::FromRequest(
                JsonRequest(body),
                "upload-123"
            );

            ASSERT_TRUE(parsed.has_value());
            EXPECT_EQ(parsed->expected_state_version, 8U);
            EXPECT_EQ(parsed->reason, "objects remain");

            body.removeMember("expected_state_version");
            EXPECT_FALSE(
                UploadCleanupRebuildRequest::FromRequest(
                    JsonRequest(body),
                    "upload-123"
                )
                    .has_value()
            );
        }

        TEST(StorageReconciliationEnqueueRequestTest, AcceptsOnlyFixedScopes) {
            constexpr std::array scopes{
                std::pair{ "contents", disk::reconciliation::ReconciliationScope::Contents },
                std::pair{    "users",    disk::reconciliation::ReconciliationScope::Users },
                std::pair{  "staging",  disk::reconciliation::ReconciliationScope::Staging },
                std::pair{    "final",    disk::reconciliation::ReconciliationScope::Final },
            };
            for (const auto& [name, expected] : scopes) {
                Json::Value body(Json::objectValue);
                body["scope"] = name;
                auto parsed = StorageReconciliationEnqueueRequest::FromRequest(
                    JsonRequest(body),
                    "ops-20260720"
                );
                ASSERT_TRUE(parsed.has_value()) << name;
                EXPECT_EQ(parsed->scope, expected);
                EXPECT_TRUE(parsed->dry_run);
            }

            Json::Value invalid(Json::objectValue);
            invalid["scope"] = "all";
            EXPECT_FALSE(
                StorageReconciliationEnqueueRequest::FromRequest(
                    JsonRequest(invalid),
                    "ops-20260720"
                )
                    .has_value()
            );
        }

        TEST(StorageReconciliationEnqueueRequestTest, ActualRunRequiresExactScanAndReason) {
            Json::Value body(Json::objectValue);
            body["scope"] = "staging";
            body["dry_run"] = false;
            body["confirm_scan_id"] = "ops-20260720";
            body["reason"] = "  storage incident  ";

            auto parsed = StorageReconciliationEnqueueRequest::FromRequest(
                JsonRequest(body),
                "ops-20260720"
            );
            ASSERT_TRUE(parsed.has_value());
            EXPECT_EQ(parsed->confirm_scan_id, "ops-20260720");
            EXPECT_EQ(parsed->reason, "storage incident");

            body["confirm_scan_id"] = "ops-other";
            EXPECT_FALSE(
                StorageReconciliationEnqueueRequest::FromRequest(
                    JsonRequest(body),
                    "ops-20260720"
                )
                    .has_value()
            );
        }

        TEST(StorageRecoveryAdminResponseTest, SerializesStableCommandResults) {
            UploadLeaseReleaseResponse lease{
                .upload_id = "upload-123",
                .dry_run = false,
                .eligible = true,
                .released = true,
                .status = disk::upload::UploadTaskStatus::Finalizing,
                .state_version = 8,
                .lease_owner = "api-a",
                .lease_expires_at = "2026-07-20 12:00:00",
                .lease_expired = true,
            };
            const auto lease_json = lease.ToJson();
            EXPECT_EQ(lease_json["status"].asString(), "finalizing");
            EXPECT_TRUE(lease_json["released"].asBool());
            EXPECT_EQ(lease_json["state_version"].asUInt64(), 8U);

            UploadCleanupRebuildResponse cleanup{
                .upload_id = "upload-123",
                .eligible = true,
                .status = disk::upload::UploadTaskStatus::Completed,
                .state_version = 9,
                .planned_action = "create",
                .job_id = 42,
                .job_status = disk::jobs::StorageJobStatus::Pending,
            };
            const auto cleanup_json = cleanup.ToJson();
            EXPECT_EQ(cleanup_json["planned_action"].asString(), "create");
            EXPECT_EQ(cleanup_json["job_status"].asString(), "pending");

            StorageReconciliationEnqueueResponse reconciliation{
                .scan_id = "ops-20260720",
                .scope = disk::reconciliation::ReconciliationScope::Staging,
                .eligible = true,
                .page_size = 1000,
                .dedupe_key = "periodic:storage-reconcile:key",
            };
            const auto reconciliation_json = reconciliation.ToJson();
            EXPECT_EQ(reconciliation_json["scope"].asString(), "staging");
            EXPECT_EQ(reconciliation_json["page_size"].asUInt64(), 1000U);
            EXPECT_TRUE(reconciliation_json["job_id"].isNull());
        }
    } // namespace
} // namespace disk::admin
