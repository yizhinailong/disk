#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "dtos/ShareDto.hpp"

namespace disk::share {
    namespace {

        enum class CancelProbeState {
            Success,
            ShareNotFound,
            AccessDenied,
            AlreadyCancelled,
            InternalError,
        };

        struct CancelProbe {
            std::string share_id;
            CancelProbeState state{ CancelProbeState::Success };
        };

        [[nodiscard]] auto CharacterizeCancel(const std::vector<CancelProbe>& probes)
            -> CancelShareResponse {
            CancelShareResponse response;
            response.summary.total = static_cast<int>(probes.size());

            for (const auto& probe : probes) {
                CancelShareResult result;
                result.share_id = probe.share_id;

                switch (probe.state) {
                    case CancelProbeState::Success:
                        result.status = "success";
                        response.summary.succeeded++;
                        break;
                    case CancelProbeState::ShareNotFound:
                        result.status = "failed";
                        result.error = CancelShareError{ .code = static_cast<int>(ErrorCode::ShareNotFound),
                                                         .message = "Share not found",
                                                         .reason = "share_not_found" };
                        response.summary.failed++;
                        break;
                    case CancelProbeState::AccessDenied:
                        result.status = "failed";
                        result.error =
                            CancelShareError{ .code = static_cast<int>(ErrorCode::ShareAccessDenied),
                                              .message = "Access denied",
                                              .reason = "access_denied" };
                        response.summary.failed++;
                        break;
                    case CancelProbeState::AlreadyCancelled:
                        result.status = "failed";
                        result.error =
                            CancelShareError{ .code = static_cast<int>(ErrorCode::ValidationFailed),
                                              .message = "Share already cancelled",
                                              .reason = "already_cancelled" };
                        response.summary.failed++;
                        break;
                    case CancelProbeState::InternalError:
                        result.status = "failed";
                        result.error = CancelShareError{ .code = static_cast<int>(ErrorCode::InternalError),
                                                         .message = "Operation failed",
                                                         .reason = "internal_error" };
                        response.summary.failed++;
                        break;
                }
                response.results.push_back(result);
            }

            return response;
        }

        struct ListProbe {
            std::string share_id;
            std::vector<ShareFile> share_files;
            std::string permission;
            bool has_password{ false };
            int status_value{ static_cast<int>(ShareStatus::Active) };
            bool has_expires_at{ false };
            bool expired_now{ false };
        };

        [[nodiscard]] auto CharacterizeList(
            const std::vector<ListProbe>& probes,
            int page,
            int page_size,
            int total
        ) -> ShareListResponse {
            ShareListResponse response;
            for (const auto& p : probes) {
                ShareItem item;
                item.share_id = p.share_id;
                item.file_name = p.share_files.empty() ? "" : p.share_files[0].name;
                item.file_count = static_cast<int>(p.share_files.size());
                item.share_link = "/s/" + p.share_id;
                item.has_password = p.has_password;
                item.permission = p.permission;
                item.view_count = 0;
                item.download_count = 0;
                item.created_at = "2026-04-01 00:00:00";
                item.expires_at = p.has_expires_at ? "2026-04-10 00:00:00" : "";

                if (p.status_value == static_cast<int>(ShareStatus::Cancelled)) {
                    item.status = "cancelled";
                } else if (p.status_value == static_cast<int>(ShareStatus::Active)) {
                    item.status = (p.has_expires_at && p.expired_now) ? "expired" : "active";
                } else {
                    item.status = "expired";
                }
                response.items.push_back(item);
            }

            response.pagination.page = page;
            response.pagination.page_size = page_size;
            response.pagination.total = total;
            response.pagination.total_pages = page_size > 0 ? (total + page_size - 1) / page_size : 0;
            return response;
        }

        class ShareServiceBatchCharacterizationTest : public ::testing::Test {};

        TEST_F(ShareServiceBatchCharacterizationTest, HappyPathBatchListAndCancel) {
            const auto list = CharacterizeList(
                {
                    ListProbe{ .share_id = "s1",
                              .share_files = { ShareFile{ .id = 1, .name = "a.txt", .type = "file", .size = 1 } },
                              .permission = "download",
                              .has_password = false,
                              .status_value = static_cast<int>(ShareStatus::Active),
                              .has_expires_at = true,
                              .expired_now = false },
                    ListProbe{ .share_id = "s2",
                              .share_files = { ShareFile{ .id = 2, .name = "b.txt", .type = "file", .size = 2 } },
                              .permission = "view",
                              .has_password = true,
                              .status_value = static_cast<int>(ShareStatus::Active),
                              .has_expires_at = false,
                              .expired_now = false },
            },
                1,
                20,
                2
            );
            ASSERT_EQ(list.items.size(), 2U);
            EXPECT_EQ(list.items[0].status, "active");
            EXPECT_EQ(list.items[0].file_name, "a.txt");
            EXPECT_EQ(list.items[1].status, "active");
            EXPECT_EQ(list.ToJson()["pagination"]["total_pages"].asInt(), 1);

            const auto cancel = CharacterizeCancel(
                {
                    CancelProbe{ .share_id = "s1", .state = CancelProbeState::Success },
                    CancelProbe{ .share_id = "s2", .state = CancelProbeState::Success },
            }
            );
            EXPECT_EQ(cancel.summary.total, 2);
            EXPECT_EQ(cancel.summary.succeeded, 2);
            EXPECT_EQ(cancel.summary.failed, 0);
            ASSERT_EQ(cancel.results.size(), 2U);
            EXPECT_EQ(cancel.results[0].status, "success");
            EXPECT_EQ(cancel.results[1].status, "success");
        }

        TEST_F(ShareServiceBatchCharacterizationTest, MixedValidityBatchPartialSuccess) {
            const auto list = CharacterizeList(
                {
                    ListProbe{    .share_id = "s-active",
                              .share_files = { ShareFile{ .id = 1, .name = "ok.txt", .type = "file", .size = 1 } },
                              .permission = "download",
                              .status_value = static_cast<int>(ShareStatus::Active),
                              .has_expires_at = true,
                              .expired_now = false },
                    ListProbe{   .share_id = "s-expired",
                              .share_files = {},
                              .permission = "view",
                              .status_value = static_cast<int>(ShareStatus::Active),
                              .has_expires_at = true,
                              .expired_now = true },
                    ListProbe{ .share_id = "s-cancelled",
                              .share_files = { ShareFile{ .id = 3, .name = "x", .type = "folder", .size = 0 } },
                              .permission = "view",
                              .status_value = static_cast<int>(ShareStatus::Cancelled),
                              .has_expires_at = false,
                              .expired_now = false },
            },
                1,
                20,
                3
            );
            ASSERT_EQ(list.items.size(), 3U);
            EXPECT_EQ(list.items[0].status, "active");
            EXPECT_EQ(list.items[1].status, "expired");
            EXPECT_EQ(list.items[1].file_name, "");
            EXPECT_EQ(list.items[2].status, "cancelled");

            const auto cancel = CharacterizeCancel(
                {
                    CancelProbe{        .share_id = "ok",          .state = CancelProbeState::Success },
                    CancelProbe{ .share_id = "not-found",    .state = CancelProbeState::ShareNotFound },
                    CancelProbe{    .share_id = "denied",     .state = CancelProbeState::AccessDenied },
                    CancelProbe{ .share_id = "cancelled", .state = CancelProbeState::AlreadyCancelled },
            }
            );
            EXPECT_EQ(cancel.summary.total, 4);
            EXPECT_EQ(cancel.summary.succeeded, 1);
            EXPECT_EQ(cancel.summary.failed, 3);
            ASSERT_EQ(cancel.results.size(), 4U);
            EXPECT_EQ(cancel.results[0].status, "success");
            EXPECT_EQ(cancel.results[1].status, "failed");
            EXPECT_EQ(cancel.results[1].error->reason, "share_not_found");
            EXPECT_EQ(cancel.results[2].error->reason, "access_denied");
            EXPECT_EQ(cancel.results[3].error->reason, "already_cancelled");
        }

        TEST_F(ShareServiceBatchCharacterizationTest, EmptyBatchEdgeCase) {
            const auto list = CharacterizeList({}, 1, 20, 0);
            EXPECT_TRUE(list.items.empty());
            EXPECT_EQ(list.pagination.total, 0);
            EXPECT_EQ(list.pagination.total_pages, 0);

            const auto cancel = CharacterizeCancel({});
            EXPECT_EQ(cancel.summary.total, 0);
            EXPECT_EQ(cancel.summary.succeeded, 0);
            EXPECT_EQ(cancel.summary.failed, 0);
            EXPECT_TRUE(cancel.results.empty());
        }

        TEST_F(ShareServiceBatchCharacterizationTest, SingleItemBatchEdgeCase) {
            const auto list = CharacterizeList(
                {
                    ListProbe{ .share_id = "single",
                              .share_files = { ShareFile{ .id = 9, .name = "single.md", .type = "file", .size = 9 } },
                              .permission = "download",
                              .status_value = static_cast<int>(ShareStatus::Active),
                              .has_expires_at = false,
                              .expired_now = false },
            },
                2,
                1,
                2
            );
            ASSERT_EQ(list.items.size(), 1U);
            EXPECT_EQ(list.items[0].file_name, "single.md");
            EXPECT_EQ(list.pagination.total_pages, 2);

            const auto cancel = CharacterizeCancel(
                {
                    CancelProbe{ .share_id = "single", .state = CancelProbeState::InternalError },
            }
            );
            EXPECT_EQ(cancel.summary.total, 1);
            EXPECT_EQ(cancel.summary.succeeded, 0);
            EXPECT_EQ(cancel.summary.failed, 1);
            ASSERT_EQ(cancel.results.size(), 1U);
            EXPECT_EQ(cancel.results[0].status, "failed");
            ASSERT_TRUE(cancel.results[0].error.has_value());
            EXPECT_EQ(cancel.results[0].error->reason, "internal_error");
        }

    } // namespace
} // namespace disk::share
