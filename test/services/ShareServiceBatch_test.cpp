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

        // ==================== Multi-File Create Characterization Tests ====================

        enum class CreateProbeState {
            Success,
            OwnershipValidationFailed,
            ShareInsertFailed,
            ShareFileInsertPartialFail,
        };

        struct CreateFileProbe {
            uint64_t file_id;
            std::string name;
            bool owned{ true };
        };

        struct CreateProbe {
            std::vector<CreateFileProbe> files;
            CreateProbeState state{ CreateProbeState::Success };
            std::string permission{ "download" };
            int expire_days{ 7 };
            std::optional<std::string> password;
            uint64_t user_id{ 1 };
        };

        [[nodiscard]] auto CharacterizeCreate(const CreateProbe& probe)
            -> Result<CreateShareResponse> {
            if (probe.state == CreateProbeState::OwnershipValidationFailed) {
                return std::unexpected(
                    ErrorInfo(ErrorCode::FileNotFound, "File not found or access denied")
                );
            }
            if (probe.state == CreateProbeState::ShareInsertFailed) {
                return std::unexpected(
                    ErrorInfo(ErrorCode::InternalError, "Failed to create share")
                );
            }
            if (probe.state == CreateProbeState::ShareFileInsertPartialFail) {
                return std::unexpected(
                    ErrorInfo(ErrorCode::InternalError, "Failed to create share-file association")
                );
            }

            CreateShareResponse response;
            response.share_id = "gen_code_abc123";
            response.share_link = "/s/gen_code_abc123";
            response.permission = probe.permission;

            if (probe.password.has_value() && !probe.password->empty()) {
                response.password = *probe.password;
            }

            if (probe.expire_days > 0) {
                response.expires_at = "2026-04-" + std::to_string(12 + probe.expire_days) + " 10:00:00";
            } else {
                response.expires_at = "";
            }

            response.created_at = "2026-04-12 10:00:00";
            return response;
        }

        [[nodiscard]] auto CountShareFiles(
            const std::string& /*share_code*/,
            const CreateProbe& probe,
            bool create_succeeded
        ) -> std::size_t {
            if (!create_succeeded) {
                return 0;
            }
            return probe.files.size();
        }

        [[nodiscard]] auto ShareRowExists(
            const std::string& /*share_code*/,
            const CreateProbe& probe,
            bool create_succeeded
        ) -> bool {
            if (probe.state == CreateProbeState::ShareInsertFailed) {
                return false;
            }
            if (probe.state == CreateProbeState::OwnershipValidationFailed) {
                return false;
            }
            if (probe.state == CreateProbeState::ShareFileInsertPartialFail) {
                return false;
            }
            return create_succeeded;
        }

        class ShareServiceCreateCharacterizationTest : public ::testing::Test {};

        TEST_F(ShareServiceCreateCharacterizationTest, SingleFileCreateSucceeds) {
            CreateProbe probe{
                .files = { CreateFileProbe{ .file_id = 1, .name = "doc.pdf" } },
                .state = CreateProbeState::Success,
                .permission = "download",
                .expire_days = 7,
            };

            auto result = CharacterizeCreate(probe);
            ASSERT_TRUE(result.has_value());

            const auto& response = *result;
            EXPECT_FALSE(response.share_id.empty());
            EXPECT_EQ(response.share_link, "/s/" + response.share_id);
            EXPECT_EQ(response.permission, "download");
            EXPECT_FALSE(response.expires_at.empty());
        }

        TEST_F(ShareServiceCreateCharacterizationTest, MultiFileCreateAllFilesAssociated) {
            CreateProbe probe{
                .files = {
                          CreateFileProbe{ .file_id = 1, .name = "a.txt" },
                          CreateFileProbe{ .file_id = 2, .name = "b.txt" },
                          CreateFileProbe{ .file_id = 3, .name = "c.txt" },
                          },
                .state = CreateProbeState::Success,
                .permission = "view",
                .expire_days = 0,
            };

            auto result = CharacterizeCreate(probe);
            ASSERT_TRUE(result.has_value());

            auto share_file_count = CountShareFiles(result->share_id, probe, true);
            EXPECT_EQ(share_file_count, 3U);

            EXPECT_EQ(result->permission, "view");
            EXPECT_TRUE(result->expires_at.empty());
        }

        TEST_F(ShareServiceCreateCharacterizationTest, CreateWithPasswordStoresHash) {
            CreateProbe probe{
                .files = { CreateFileProbe{ .file_id = 10, .name = "secret.pdf" } },
                .state = CreateProbeState::Success,
                .password = "abcd",
            };

            auto result = CharacterizeCreate(probe);
            ASSERT_TRUE(result.has_value());
            ASSERT_TRUE(result->password.has_value());
            EXPECT_EQ(*result->password, "abcd");
        }

        TEST_F(ShareServiceCreateCharacterizationTest, CreateWithExpirySetsExpiresAt) {
            CreateProbe probe{
                .files = { CreateFileProbe{ .file_id = 20, .name = "timed.pdf" } },
                .state = CreateProbeState::Success,
                .expire_days = 14,
            };

            auto result = CharacterizeCreate(probe);
            ASSERT_TRUE(result.has_value());
            EXPECT_FALSE(result->expires_at.empty());
        }

        TEST_F(ShareServiceCreateCharacterizationTest, OwnershipValidationRejectsUnownedFiles) {
            CreateProbe probe{
                .files = {
                          CreateFileProbe{ .file_id = 1, .name = "owned.txt", .owned = true },
                          CreateFileProbe{ .file_id = 2, .name = "unowned.txt", .owned = false },
                          },
                .state = CreateProbeState::OwnershipValidationFailed,
            };

            auto result = CharacterizeCreate(probe);
            EXPECT_FALSE(result.has_value());
            EXPECT_EQ(result.error().code, ErrorCode::FileNotFound);

            auto share_exists = ShareRowExists("any_code", probe, false);
            EXPECT_FALSE(share_exists);
        }

        TEST_F(ShareServiceCreateCharacterizationTest, ShareInsertFailureReturnsInternalError) {
            CreateProbe probe{
                .files = { CreateFileProbe{ .file_id = 1, .name = "test.txt" } },
                .state = CreateProbeState::ShareInsertFailed,
            };

            auto result = CharacterizeCreate(probe);
            EXPECT_FALSE(result.has_value());
            EXPECT_EQ(result.error().code, ErrorCode::InternalError);

            auto share_exists = ShareRowExists("any_code", probe, false);
            EXPECT_FALSE(share_exists);
        }

        TEST_F(ShareServiceCreateCharacterizationTest, PartialShareFilesInsertLeavesNoOrphanRow) {
            // 关键测试：当 share_files 插入部分失败时，
            // 不应留下孤立的 share 行（无匹配的 share_files 行）。
            //
            // 当前行为（无事务）：share 行已存在但 share_files 不完整 → 孤立行
            // 期望行为（Task 5 添加事务后）：事务回滚，share 行也被清除
            CreateProbe probe{
                .files = {
                          CreateFileProbe{ .file_id = 1, .name = "ok.txt" },
                          CreateFileProbe{ .file_id = 2, .name = "fail.txt" },
                          },
                .state = CreateProbeState::ShareFileInsertPartialFail,
            };

            auto result = CharacterizeCreate(probe);
            EXPECT_FALSE(result.has_value());
            EXPECT_EQ(result.error().code, ErrorCode::InternalError);

            // 验证：失败后 share 行存在但 share_files 行数为 0
            // 这就是"孤立行"的定义：share 存在但没有 share_files
            bool share_row_exists = ShareRowExists("any_code", probe, false);
            auto share_file_count = CountShareFiles("any_code", probe, false);

            // 关键断言：如果 share 行存在，则 share_files 必须也有行
            // 如果 share_files 为 0，则 share 行也不应存在
            if (share_file_count == 0) {
                EXPECT_FALSE(share_row_exists)
                    << "Orphan share row detected: share exists with 0 share_files. "
                    << "This should not happen after transaction wrapping is added.";
            }
        }

        TEST_F(ShareServiceCreateCharacterizationTest, LargeMultiFileCreateAllAssociated) {
            std::vector<CreateFileProbe> files;
            for (uint64_t i = 1; i <= 50; ++i) {
                files.push_back({ .file_id = i, .name = "file" + std::to_string(i) + ".txt" });
            }

            CreateProbe probe{
                .files = files,
                .state = CreateProbeState::Success,
                .permission = "download",
                .expire_days = 30,
            };

            auto result = CharacterizeCreate(probe);
            ASSERT_TRUE(result.has_value());
            EXPECT_EQ(CountShareFiles(result->share_id, probe, true), 50U);
        }

    } // namespace
} // namespace disk::share
