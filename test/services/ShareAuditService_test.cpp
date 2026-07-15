#include "services/ShareAuditService.hpp"

#include <array>
#include <string_view>

#include <gtest/gtest.h>

namespace disk::share {
    namespace {

        auto ExpectNoCredentialFields(const Json::Value& details) -> void {
            constexpr std::array<std::string_view, 8> forbidden_fields{
                "password",
                "password_hash",
                "share_token",
                "authorization",
                "x-share-token",
                "ip",
                "ip_address",
                "user_agent",
            };
            for (const auto field : forbidden_fields) {
                EXPECT_FALSE(details.isMember(std::string(field))) << field;
            }
        }

        TEST(ShareAuditServiceTest, CreateDetailsUseStableTypedFields) {
            const auto details = ShareCreateAuditEvent{
                .actor_user_id = 7,
                .share_id = 11,
                .share_code = "audit001",
                .file_ids = { 17, 18 },
                .folder_ids = { 19 },
                .permission = "download",
                .expires_at = std::nullopt,
                .context = { .ip_address = "127.0.0.1", .user_agent = "audit-test" },
            }
                                     .ToDetails();

            EXPECT_EQ(details["share_code"].asString(), "audit001");
            ASSERT_EQ(details["file_ids"].size(), 2U);
            EXPECT_EQ(details["file_ids"][0].asUInt64(), 17U);
            ASSERT_EQ(details["folder_ids"].size(), 1U);
            EXPECT_EQ(details["permission"].asString(), "download");
            EXPECT_TRUE(details["expires_at"].isNull());
            EXPECT_TRUE(details["success"].asBool());
            EXPECT_EQ(details["result"].asString(), "success");
            ExpectNoCredentialFields(details);
        }

        TEST(ShareAuditServiceTest, AccessDetailsRecordResultWithoutRequestCredentials) {
            const auto details = ShareAccessAuditEvent{
                .share_id = 11,
                .share_code = "audit001",
                .success = false,
                .result = "validation_failed",
                .context = { .ip_address = "127.0.0.1", .user_agent = "audit-test" },
            }
                                     .ToDetails();

            EXPECT_FALSE(details["success"].asBool());
            EXPECT_EQ(details["result"].asString(), "validation_failed");
            ExpectNoCredentialFields(details);
        }

        TEST(ShareAuditServiceTest, PasswordFailureDetailsExposeCounterState) {
            const auto details = SharePasswordFailureAuditEvent{
                .share_id = std::nullopt,
                .share_code = "missing1",
                .attempt_count = 6,
                .counter_available = true,
                .rate_limited = true,
                .context = {},
            }
                                     .ToDetails();

            EXPECT_EQ(details["attempt_count"].asUInt64(), 6U);
            EXPECT_TRUE(details["counter_available"].asBool());
            EXPECT_TRUE(details["rate_limited"].asBool());
            EXPECT_FALSE(details["success"].asBool());
            EXPECT_EQ(details["result"].asString(), "rate_limited");
            ExpectNoCredentialFields(details);
        }

        TEST(ShareAuditServiceTest, DownloadDetailsRecordSelectedResponseBytes) {
            const auto details = ShareDownloadAuditEvent{
                .share_id = 11,
                .share_code = "audit001",
                .file_id = 21,
                .bytes = 512,
                .http_status = 206,
                .success = true,
                .result = "partial_content",
                .context = {},
            }
                                     .ToDetails();

            EXPECT_EQ(details["file_id"].asUInt64(), 21U);
            EXPECT_EQ(details["bytes"].asUInt64(), 512U);
            EXPECT_EQ(details["http_status"].asInt(), 206);
            EXPECT_TRUE(details["success"].asBool());
            EXPECT_EQ(details["result"].asString(), "partial_content");
            ExpectNoCredentialFields(details);
        }

        TEST(ShareAuditServiceTest, CancelDetailsRecordActorAndPerItemResult) {
            const auto details = ShareCancelAuditEvent{
                .actor_user_id = 7,
                .share_id = std::nullopt,
                .share_code = "missing1",
                .success = false,
                .result = "share_not_found",
                .context = {},
            }
                                     .ToDetails();

            EXPECT_EQ(details["cancelled_by"].asUInt64(), 7U);
            EXPECT_FALSE(details["success"].asBool());
            EXPECT_EQ(details["result"].asString(), "share_not_found");
            ExpectNoCredentialFields(details);
        }

    } // namespace
} // namespace disk::share
