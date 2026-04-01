#include <cstdint>
#include <vector>

#include <gtest/gtest.h>

#include "dtos/FileDto.hpp"

namespace disk::file {
    namespace {

        struct MoveProbe {
            uint64_t file_id{ 0 };
            bool found_and_owned{ false };
            bool already_in_target{ false };
            bool filename_conflict{ false };
            bool update_ok{ true };
        };

        struct CopyProbe {
            uint64_t file_id{ 0 };
            bool found_and_owned{ false };
            uint64_t size{ 0 };
            bool filename_conflict{ false };
            bool content_ref_increment_ok{ true };
            bool insert_ok{ true };
            uint64_t new_id{ 0 };
        };

        struct DeleteProbe {
            uint64_t file_id{ 0 };
            bool found_and_owned{ false };
            bool trash_insert_ok{ true };
            bool delete_ok{ true };
        };

        [[nodiscard]] auto CharacterizeMove(const std::vector<MoveProbe>& probes) -> MoveResponse {
            MoveResponse response;
            for (const auto& p : probes) {
                if (!p.found_and_owned) {
                    continue;
                }
                if (p.already_in_target) {
                    response.moved_count++;
                    continue;
                }
                if (p.filename_conflict) {
                    continue;
                }
                if (p.update_ok) {
                    response.moved_count++;
                }
            }
            return response;
        }

        [[nodiscard]] auto CharacterizeCopy(const std::vector<CopyProbe>& probes) -> CopyResponse {
            uint64_t total_copy_size = 0;
            std::vector<CopyProbe> files_to_copy;
            for (const auto& p : probes) {
                if (!p.found_and_owned) {
                    continue;
                }
                total_copy_size += p.size;
                files_to_copy.push_back(p);
            }

            CopyResponse response;
            if (total_copy_size == 0) {
                response.copied_count = 0;
                response.new_files = {};
                return response;
            }

            for (const auto& p : files_to_copy) {
                if (p.filename_conflict) {
                    continue;
                }
                if (!p.content_ref_increment_ok) {
                    continue;
                }
                if (!p.insert_ok) {
                    continue;
                }
                response.copied_count++;
                response.new_files.push_back(FileIdMapping{ .old_id = p.file_id, .new_id = p.new_id });
            }
            return response;
        }

        [[nodiscard]] auto CharacterizeDelete(const std::vector<DeleteProbe>& probes) -> DeleteResponse {
            DeleteResponse response;
            for (const auto& p : probes) {
                if (!p.found_and_owned) {
                    continue;
                }
                if (p.trash_insert_ok && p.delete_ok) {
                    response.deleted_count++;
                }
            }
            return response;
        }

        class FileServiceBatchCharacterizationTest : public ::testing::Test {};

        TEST_F(FileServiceBatchCharacterizationTest, HappyPathBatchAllValid) {
            const auto move = CharacterizeMove({
                MoveProbe{ .file_id = 1, .found_and_owned = true,         .update_ok = true },
                MoveProbe{ .file_id = 2, .found_and_owned = true, .already_in_target = true },
            });
            EXPECT_EQ(move.moved_count, 2);
            EXPECT_EQ(move.ToJson()["moved_count"].asInt(), 2);

            const auto copy = CharacterizeCopy({
                CopyProbe{ .file_id = 10, .found_and_owned = true, .size = 100, .content_ref_increment_ok = true, .insert_ok = true, .new_id = 1010 },
                CopyProbe{ .file_id = 11, .found_and_owned = true, .size = 200, .content_ref_increment_ok = true, .insert_ok = true, .new_id = 1011 },
            });
            EXPECT_EQ(copy.copied_count, 2);
            ASSERT_EQ(copy.new_files.size(), 2U);
            EXPECT_EQ(copy.new_files[0].old_id, 10U);
            EXPECT_EQ(copy.new_files[0].new_id, 1010U);
            EXPECT_EQ(copy.ToJson()["new_files"].size(), 2U);

            const auto del = CharacterizeDelete({
                DeleteProbe{ .file_id = 30, .found_and_owned = true },
                DeleteProbe{ .file_id = 31, .found_and_owned = true },
            });
            EXPECT_EQ(del.deleted_count, 2);
            EXPECT_EQ(del.ToJson()["deleted_count"].asInt(), 2);
        }

        TEST_F(FileServiceBatchCharacterizationTest, MixedValidityBatchPartialSuccess) {
            const auto move = CharacterizeMove({
                MoveProbe{ .file_id = 1, .found_and_owned = true, .update_ok = true },
                MoveProbe{ .file_id = 2, .found_and_owned = false },
                MoveProbe{ .file_id = 3, .found_and_owned = true, .filename_conflict = true },
                MoveProbe{ .file_id = 4, .found_and_owned = true, .already_in_target = true },
            });
            EXPECT_EQ(move.moved_count, 2);

            const auto copy = CharacterizeCopy({
                CopyProbe{ .file_id = 10, .found_and_owned = false, .size = 100, .new_id = 1010 },
                CopyProbe{ .file_id = 11, .found_and_owned = true, .size = 200, .filename_conflict = true, .new_id = 1011 },
                CopyProbe{ .file_id = 12, .found_and_owned = true, .size = 300, .content_ref_increment_ok = false, .new_id = 1012 },
                CopyProbe{ .file_id = 13, .found_and_owned = true, .size = 400, .content_ref_increment_ok = true, .insert_ok = true, .new_id = 1013 },
            });
            EXPECT_EQ(copy.copied_count, 1);
            ASSERT_EQ(copy.new_files.size(), 1U);
            EXPECT_EQ(copy.new_files[0].old_id, 13U);

            const auto del = CharacterizeDelete({
                DeleteProbe{ .file_id = 30, .found_and_owned = true, .trash_insert_ok = true, .delete_ok = true },
                DeleteProbe{ .file_id = 31, .found_and_owned = true, .trash_insert_ok = false, .delete_ok = true },
                DeleteProbe{ .file_id = 32, .found_and_owned = false },
            });
            EXPECT_EQ(del.deleted_count, 1);
        }

        TEST_F(FileServiceBatchCharacterizationTest, EmptyBatchEdgeCase) {
            const auto move = CharacterizeMove({});
            const auto copy = CharacterizeCopy({});
            const auto del = CharacterizeDelete({});

            EXPECT_EQ(move.moved_count, 0);
            EXPECT_EQ(copy.copied_count, 0);
            EXPECT_TRUE(copy.new_files.empty());
            EXPECT_EQ(del.deleted_count, 0);
        }

        TEST_F(FileServiceBatchCharacterizationTest, SingleItemBatchEdgeCase) {
            const auto move = CharacterizeMove({
                MoveProbe{ .file_id = 1, .found_and_owned = true, .update_ok = true },
            });
            EXPECT_EQ(move.moved_count, 1);

            const auto copy = CharacterizeCopy({
                CopyProbe{ .file_id = 7, .found_and_owned = true, .size = 123, .content_ref_increment_ok = true, .insert_ok = true, .new_id = 7007 },
            });
            EXPECT_EQ(copy.copied_count, 1);
            ASSERT_EQ(copy.new_files.size(), 1U);
            EXPECT_EQ(copy.new_files[0].old_id, 7U);
            EXPECT_EQ(copy.new_files[0].new_id, 7007U);

            const auto del = CharacterizeDelete({
                DeleteProbe{ .file_id = 8, .found_and_owned = true },
            });
            EXPECT_EQ(del.deleted_count, 1);
        }

    } // namespace
} // namespace disk::file
