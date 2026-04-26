#include <cstdint>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "dtos/TrashDto.hpp"

namespace disk::trash {
    namespace {

        enum class ItemType {
            File,
            Folder,
            Unknown,
        };

        struct RestoreProbe {
            uint64_t trash_id{ 0 };
            bool exists{ true };
            bool owned_by_user{ true };
            ItemType item_type{ ItemType::File };
            bool op_success{ true };
            uint64_t restored_id{ 0 };
            std::string path;
        };

        struct DeleteProbe {
            uint64_t trash_id{ 0 };
            bool exists{ true };
            bool owned_by_user{ true };
            ItemType item_type{ ItemType::File };
            bool op_success{ true };
            uint64_t freed_space{ 0 };
        };

        struct DeleteAllProbe {
            ItemType item_type{ ItemType::File };
            bool op_success{ true };
            uint64_t freed_space{ 0 };
        };

        [[nodiscard]] auto CharacterizeRestore(const std::vector<RestoreProbe>& probes)
            -> BatchRestoreResponse {
            BatchRestoreResponse response;
            response.summary.total = static_cast<int>(probes.size());

            for (const auto& p : probes) {
                BatchResultItem result;
                result.trash_id = p.trash_id;

                if (!p.exists || !p.owned_by_user) {
                    result.status = "failed";
                    result.code = static_cast<uint16_t>(ErrorCode::ResourceNotFound);
                    result.message = "Trash item not found";
                    result.field = "trash_id";
                    result.value = std::to_string(p.trash_id);
                    response.summary.failure_count++;
                    response.results.push_back(result);
                    continue;
                }

                if (p.item_type == ItemType::Unknown) {
                    result.status = "failed";
                    result.code = static_cast<uint16_t>(ErrorCode::ValidationFailed);
                    result.message = "Unknown item type";
                    response.summary.failure_count++;
                    response.results.push_back(result);
                    continue;
                }

                if (p.op_success) {
                    result.status = "success";
                    result.path = p.path;
                    if (p.item_type == ItemType::File) {
                        result.file_id = p.restored_id;
                    } else {
                        result.folder_id = p.restored_id;
                    }
                    response.summary.success_count++;
                } else {
                    result.status = "failed";
                    result.code = static_cast<uint16_t>(ErrorCode::InternalError);
                    result.message = "Operation failed";
                    response.summary.failure_count++;
                }
                response.results.push_back(result);
            }

            return response;
        }

        [[nodiscard]] auto CharacterizeDelete(const std::vector<DeleteProbe>& probes)
            -> BatchDeleteResponse {
            BatchDeleteResponse response;
            response.summary.total = static_cast<int>(probes.size());

            for (const auto& p : probes) {
                BatchResultItem result;
                result.trash_id = p.trash_id;

                if (!p.exists || !p.owned_by_user) {
                    result.status = "failed";
                    result.code = static_cast<uint16_t>(ErrorCode::ResourceNotFound);
                    result.message = "Trash item not found";
                    result.field = "trash_id";
                    result.value = std::to_string(p.trash_id);
                    response.summary.failure_count++;
                    response.results.push_back(result);
                    continue;
                }

                if (p.item_type == ItemType::Unknown) {
                    result.status = "failed";
                    result.code = static_cast<uint16_t>(ErrorCode::ValidationFailed);
                    result.message = "Unknown item type";
                    response.summary.failure_count++;
                    response.results.push_back(result);
                    continue;
                }

                if (p.op_success) {
                    result.status = "success";
                    result.freed_space = p.freed_space;
                    response.summary.success_count++;
                } else {
                    result.status = "failed";
                    result.code = static_cast<uint16_t>(ErrorCode::InternalError);
                    result.message = "Operation failed";
                    response.summary.failure_count++;
                }
                response.results.push_back(result);
            }

            return response;
        }

        [[nodiscard]] auto CharacterizeDeleteAll(const std::vector<DeleteAllProbe>& probes)
            -> DeleteAllResponse {
            DeleteAllResponse response;
            for (const auto& p : probes) {
                if (p.item_type == ItemType::Unknown) {
                    continue;
                }
                if (!p.op_success) {
                    continue;
                }
                response.deleted_count++;
                response.freed_space += p.freed_space;
            }
            return response;
        }

        class TrashServiceBatchCharacterizationTest : public ::testing::Test {};

        TEST_F(TrashServiceBatchCharacterizationTest, HappyPathBatchRestoreDeleteDeleteAll) {
            const auto restore = CharacterizeRestore(
                {
                    RestoreProbe{ .trash_id = 1,
                                 .exists = true,
                                 .owned_by_user = true,
                                 .item_type = ItemType::File,
                                 .op_success = true,
                                 .restored_id = 101,
                                 .path = "/a.txt" },
                    RestoreProbe{ .trash_id = 2,
                                 .exists = true,
                                 .owned_by_user = true,
                                 .item_type = ItemType::Folder,
                                 .op_success = true,
                                 .restored_id = 202,
                                 .path = "/docs/" },
            }
            );
            EXPECT_EQ(restore.summary.total, 2);
            EXPECT_EQ(restore.summary.success_count, 2);
            EXPECT_EQ(restore.summary.failure_count, 0);
            ASSERT_EQ(restore.results.size(), 2U);
            EXPECT_EQ(restore.results[0].status, "success");
            EXPECT_EQ(restore.results[1].status, "success");
            EXPECT_TRUE(restore.results[0].file_id.has_value());
            EXPECT_TRUE(restore.results[1].folder_id.has_value());

            const auto del = CharacterizeDelete(
                {
                    DeleteProbe{ .trash_id = 10,
                                .exists = true,
                                .owned_by_user = true,
                                .item_type = ItemType::File,
                                .op_success = true,
                                .freed_space = 10 },
                    DeleteProbe{ .trash_id = 11,
                                .exists = true,
                                .owned_by_user = true,
                                .item_type = ItemType::Folder,
                                .op_success = true,
                                .freed_space = 20 },
            }
            );
            EXPECT_EQ(del.summary.total, 2);
            EXPECT_EQ(del.summary.success_count, 2);
            EXPECT_EQ(del.summary.failure_count, 0);
            ASSERT_EQ(del.results.size(), 2U);
            EXPECT_EQ(del.results[0].freed_space.value(), 10U);
            EXPECT_EQ(del.results[1].freed_space.value(), 20U);

            const auto del_all = CharacterizeDeleteAll(
                {
                    DeleteAllProbe{   .item_type = ItemType::File, .op_success = true, .freed_space = 10 },
                    DeleteAllProbe{ .item_type = ItemType::Folder, .op_success = true, .freed_space = 20 },
            }
            );
            EXPECT_EQ(del_all.deleted_count, 2);
            EXPECT_EQ(del_all.freed_space, 30U);
        }

        TEST_F(TrashServiceBatchCharacterizationTest, MixedValidityBatchPartialSuccess) {
            const auto restore = CharacterizeRestore(
                {
                    RestoreProbe{ .trash_id = 1,
                                 .exists = true,
                                 .owned_by_user = true,
                                 .item_type = ItemType::File,
                                 .op_success = true,
                                 .restored_id = 101,
                                 .path = "/ok.txt" },
                    RestoreProbe{ .trash_id = 2,
                                 .exists = false,
                                 .owned_by_user = true,
                                 .item_type = ItemType::File,
                                 .op_success = false,
                                 .restored_id = 0,
                                 .path = ""       },
                    RestoreProbe{ .trash_id = 3,
                                 .exists = true,
                                 .owned_by_user = true,
                                 .item_type = ItemType::Unknown,
                                 .op_success = false,
                                 .restored_id = 0,
                                 .path = ""       },
            }
            );
            EXPECT_EQ(restore.summary.total, 3);
            EXPECT_EQ(restore.summary.success_count, 1);
            EXPECT_EQ(restore.summary.failure_count, 2);
            ASSERT_EQ(restore.results.size(), 3U);
            EXPECT_EQ(restore.results[0].status, "success");
            EXPECT_EQ(restore.results[1].status, "failed");
            EXPECT_EQ(restore.results[1].field.value(), "trash_id");
            EXPECT_EQ(restore.results[2].status, "failed");
            EXPECT_FALSE(restore.results[2].field.has_value());

            const auto del = CharacterizeDelete(
                {
                    DeleteProbe{ .trash_id = 10,
                                .exists = true,
                                .owned_by_user = true,
                                .item_type = ItemType::File,
                                .op_success = true,
                                .freed_space = 9 },
                    DeleteProbe{ .trash_id = 11,
                                .exists = true,
                                .owned_by_user = false,
                                .item_type = ItemType::File,
                                .op_success = false,
                                .freed_space = 0 },
                    DeleteProbe{ .trash_id = 12,
                                .exists = true,
                                .owned_by_user = true,
                                .item_type = ItemType::Unknown,
                                .op_success = false,
                                .freed_space = 0 },
            }
            );
            EXPECT_EQ(del.summary.total, 3);
            EXPECT_EQ(del.summary.success_count, 1);
            EXPECT_EQ(del.summary.failure_count, 2);
            ASSERT_EQ(del.results.size(), 3U);
            EXPECT_EQ(del.results[0].status, "success");
            EXPECT_EQ(del.results[1].status, "failed");
            EXPECT_EQ(del.results[2].status, "failed");

            const auto del_all = CharacterizeDeleteAll(
                {
                    DeleteAllProbe{    .item_type = ItemType::File,  .op_success = true,  .freed_space = 9 },
                    DeleteAllProbe{  .item_type = ItemType::Folder, .op_success = false, .freed_space = 99 },
                    DeleteAllProbe{ .item_type = ItemType::Unknown,  .op_success = true, .freed_space = 99 },
            }
            );
            EXPECT_EQ(del_all.deleted_count, 1);
            EXPECT_EQ(del_all.freed_space, 9U);
        }

        TEST_F(TrashServiceBatchCharacterizationTest, EmptyBatchEdgeCase) {
            const auto restore = CharacterizeRestore({});
            EXPECT_EQ(restore.summary.total, 0);
            EXPECT_EQ(restore.summary.success_count, 0);
            EXPECT_EQ(restore.summary.failure_count, 0);
            EXPECT_TRUE(restore.results.empty());

            const auto del = CharacterizeDelete({});
            EXPECT_EQ(del.summary.total, 0);
            EXPECT_EQ(del.summary.success_count, 0);
            EXPECT_EQ(del.summary.failure_count, 0);
            EXPECT_TRUE(del.results.empty());

            const auto del_all = CharacterizeDeleteAll({});
            EXPECT_EQ(del_all.deleted_count, 0);
            EXPECT_EQ(del_all.freed_space, 0U);
        }

        TEST_F(TrashServiceBatchCharacterizationTest, SingleItemBatchEdgeCase) {
            const auto restore = CharacterizeRestore(
                {
                    RestoreProbe{ .trash_id = 1,
                                 .exists = true,
                                 .owned_by_user = true,
                                 .item_type = ItemType::Folder,
                                 .op_success = true,
                                 .restored_id = 901,
                                 .path = "/single/" },
            }
            );
            EXPECT_EQ(restore.summary.total, 1);
            EXPECT_EQ(restore.summary.success_count, 1);
            EXPECT_EQ(restore.summary.failure_count, 0);
            ASSERT_EQ(restore.results.size(), 1U);
            EXPECT_EQ(restore.results[0].folder_id.value(), 901U);

            const auto del = CharacterizeDelete(
                {
                    DeleteProbe{ .trash_id = 2,
                                .exists = true,
                                .owned_by_user = true,
                                .item_type = ItemType::File,
                                .op_success = false,
                                .freed_space = 123 },
            }
            );
            EXPECT_EQ(del.summary.total, 1);
            EXPECT_EQ(del.summary.success_count, 0);
            EXPECT_EQ(del.summary.failure_count, 1);
            ASSERT_EQ(del.results.size(), 1U);
            EXPECT_EQ(del.results[0].status, "failed");
            EXPECT_TRUE(del.results[0].code.has_value());

            const auto del_all = CharacterizeDeleteAll(
                {
                    DeleteAllProbe{ .item_type = ItemType::Folder, .op_success = true, .freed_space = 77 },
            }
            );
            EXPECT_EQ(del_all.deleted_count, 1);
            EXPECT_EQ(del_all.freed_space, 77U);
        }

    } // namespace
} // namespace disk::trash
