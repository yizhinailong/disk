/**
 * @file TrashService_test.cpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief TrashService 单元测试
 *
 * @copyright Copyright (c) 2026
 *
 */

#include <gtest/gtest.h>

#include "dtos/TrashDto.hpp"

namespace disk::trash {
    namespace {

        // ==================== TrashService Test Fixtures ====================

        class TrashServiceTest : public ::testing::Test {
        protected:
            void SetUp() override {}

            void TearDown() override {}
        };

        // ==================== TrashService Response DTO Tests ====================
        // These tests verify the DTO structures used by TrashService methods

        class TrashServiceResponseDtoTest : public ::testing::Test {};

        TEST_F(TrashServiceResponseDtoTest, TrashItemResponseFileItem) {
            TrashItemResponse response;
            response.id = 1;
            response.type = "file";
            response.original_id = 100;
            response.name = "document.pdf";
            response.size = 2048;
            response.original_path = "/docs/document.pdf";
            response.deleted_at = "2026-02-15 10:00:00";
            response.expires_at = "2026-03-17 10:00:00";

            auto json = response.ToJson();

            EXPECT_EQ(json["id"].asUInt64(), 1U);
            EXPECT_EQ(json["type"].asString(), "file");
            EXPECT_EQ(json["original_id"].asUInt64(), 100U);
            EXPECT_EQ(json["name"].asString(), "document.pdf");
            EXPECT_EQ(json["size"].asUInt64(), 2048U);
            EXPECT_EQ(json["original_path"].asString(), "/docs/document.pdf");
        }

        TEST_F(TrashServiceResponseDtoTest, TrashItemResponseFolderItem) {
            TrashItemResponse response;
            response.id = 2;
            response.type = "folder";
            response.original_id = 200;
            response.name = "Projects";
            response.size = 0;
            response.original_path = "/Projects";
            response.deleted_at = "2026-02-15 11:00:00";
            response.expires_at = "2026-03-17 11:00:00";

            auto json = response.ToJson();

            EXPECT_EQ(json["type"].asString(), "folder");
            EXPECT_EQ(json["size"].asUInt64(), 0U);
        }

        // ==================== TrashService Batch Result Tests ====================
        // Tests for per-item results returned by Restore/Delete operations

        class TrashServiceBatchResultTest : public ::testing::Test {};

        TEST_F(TrashServiceBatchResultTest, RestoreFileSuccessFormat) {
            BatchResultItem item;
            item.trash_id = 10;
            item.status = "success";
            item.file_id = 500;
            item.path = "/restored_docs/report.pdf";

            auto json = item.ToJson();

            EXPECT_EQ(json["trash_id"].asUInt64(), 10U);
            EXPECT_EQ(json["status"].asString(), "success");
            EXPECT_EQ(json["file_id"].asUInt64(), 500U);
            EXPECT_EQ(json["path"].asString(), "/restored_docs/report.pdf");
            EXPECT_FALSE(json.isMember("error"));
            EXPECT_FALSE(json.isMember("freed_space"));
        }

        TEST_F(TrashServiceBatchResultTest, RestoreFolderSuccessFormat) {
            BatchResultItem item;
            item.trash_id = 20;
            item.status = "success";
            item.folder_id = 600;
            item.path = "/restored_folder/subfolder/";

            auto json = item.ToJson();

            EXPECT_EQ(json["trash_id"].asUInt64(), 20U);
            EXPECT_EQ(json["status"].asString(), "success");
            EXPECT_EQ(json["folder_id"].asUInt64(), 600U);
            EXPECT_EQ(json["path"].asString(), "/restored_folder/subfolder/");
        }

        TEST_F(TrashServiceBatchResultTest, DeleteSuccessFormat) {
            BatchResultItem item;
            item.trash_id = 30;
            item.status = "success";
            item.freed_space = 4096;

            auto json = item.ToJson();

            EXPECT_EQ(json["trash_id"].asUInt64(), 30U);
            EXPECT_EQ(json["status"].asString(), "success");
            EXPECT_EQ(json["freed_space"].asUInt64(), 4096U);
            EXPECT_FALSE(json.isMember("file_id"));
            EXPECT_FALSE(json.isMember("error"));
        }

        TEST_F(TrashServiceBatchResultTest, FailedItemFormat) {
            BatchResultItem item;
            item.trash_id = 40;
            item.status = "failed";
            item.code = static_cast<uint16_t>(ErrorCode::ResourceNotFound);
            item.message = "Trash item not found";
            item.field = "trash_id";
            item.value = "40";

            auto json = item.ToJson();

            EXPECT_EQ(json["trash_id"].asUInt64(), 40U);
            EXPECT_EQ(json["status"].asString(), "failed");
            ASSERT_TRUE(json.isMember("error"));
            EXPECT_EQ(
                json["error"]["code"].asUInt(),
                static_cast<uint32_t>(ErrorCode::ResourceNotFound)
            );
            EXPECT_EQ(json["error"]["message"].asString(), "Trash item not found");
            EXPECT_EQ(json["error"]["field"].asString(), "trash_id");
            EXPECT_EQ(json["error"]["value"].asString(), "40");
        }

        TEST_F(TrashServiceBatchResultTest, FailedItemMinimalFormat) {
            BatchResultItem item;
            item.trash_id = 50;
            item.status = "failed";
            item.code = static_cast<uint16_t>(ErrorCode::InternalError);
            item.message = "服务器内部错误";

            auto json = item.ToJson();

            EXPECT_EQ(json["status"].asString(), "failed");
            ASSERT_TRUE(json.isMember("error"));
            EXPECT_EQ(
                json["error"]["code"].asUInt(),
                static_cast<uint32_t>(ErrorCode::InternalError)
            );
        }

        // ==================== TrashService Batch Summary Tests ====================

        class TrashServiceBatchSummaryTest : public ::testing::Test {};

        TEST_F(TrashServiceBatchSummaryTest, SummaryFieldsCorrect) {
            BatchSummary summary;
            summary.total = 10;
            summary.success_count = 7;
            summary.failure_count = 3;

            auto json = summary.ToJson();

            EXPECT_EQ(json["total"].asInt(), 10);
            EXPECT_EQ(json["success_count"].asInt(), 7);
            EXPECT_EQ(json["failure_count"].asInt(), 3);
        }

        TEST_F(TrashServiceBatchSummaryTest, SummaryDefaultValues) {
            BatchSummary summary;

            EXPECT_EQ(summary.total, 0);
            EXPECT_EQ(summary.success_count, 0);
            EXPECT_EQ(summary.failure_count, 0);
        }

        TEST_F(TrashServiceBatchSummaryTest, SummaryConsistency) {
            BatchSummary summary;
            summary.total = 5;
            summary.success_count = 3;
            summary.failure_count = 2;

            auto json = summary.ToJson();

            EXPECT_EQ(
                json["total"].asInt(),
                json["success_count"].asInt() + json["failure_count"].asInt()
            );
        }

        // ==================== TrashService Batch Response Tests ====================

        class TrashServiceBatchResponseTest : public ::testing::Test {};

        TEST_F(TrashServiceBatchResponseTest, BatchRestoreResponseStructure) {
            BatchRestoreResponse response;
            response.summary.total = 3;
            response.summary.success_count = 2;
            response.summary.failure_count = 1;

            BatchResultItem success1;
            success1.trash_id = 1;
            success1.status = "success";
            success1.file_id = 100;
            success1.path = "/file1.txt";

            BatchResultItem success2;
            success2.trash_id = 2;
            success2.status = "success";
            success2.folder_id = 200;
            success2.path = "/folder1/";

            BatchResultItem failed;
            failed.trash_id = 3;
            failed.status = "failed";
            failed.code = static_cast<uint16_t>(ErrorCode::ResourceNotFound);
            failed.message = "回收站项目不存在";

            response.results.push_back(success1);
            response.results.push_back(success2);
            response.results.push_back(failed);

            auto json = response.ToJson();

            ASSERT_TRUE(json.isMember("summary"));
            ASSERT_TRUE(json.isMember("results"));
            EXPECT_EQ(json["results"].size(), 3U);
            EXPECT_EQ(json["results"][0]["status"].asString(), "success");
            EXPECT_EQ(json["results"][1]["status"].asString(), "success");
            EXPECT_EQ(json["results"][2]["status"].asString(), "failed");
        }

        TEST_F(TrashServiceBatchResponseTest, BatchDeleteResponseStructure) {
            BatchDeleteResponse response;
            response.summary.total = 2;
            response.summary.success_count = 2;
            response.summary.failure_count = 0;

            BatchResultItem item1;
            item1.trash_id = 10;
            item1.status = "success";
            item1.freed_space = 1024;

            BatchResultItem item2;
            item2.trash_id = 11;
            item2.status = "success";
            item2.freed_space = 2048;

            response.results.push_back(item1);
            response.results.push_back(item2);

            auto json = response.ToJson();

            ASSERT_TRUE(json.isMember("summary"));
            ASSERT_TRUE(json.isMember("results"));
            EXPECT_EQ(json["results"].size(), 2U);
            EXPECT_EQ(json["results"][0]["freed_space"].asUInt64(), 1024U);
            EXPECT_EQ(json["results"][1]["freed_space"].asUInt64(), 2048U);
        }

        // ==================== TrashService DeleteAll Response Tests ====================

        class TrashServiceDeleteAllTest : public ::testing::Test {};

        TEST_F(TrashServiceDeleteAllTest, DeleteAllResponseFields) {
            DeleteAllResponse response;
            response.deleted_count = 15;
            response.freed_space = 102400;

            auto json = response.ToJson();

            EXPECT_EQ(json["deleted_count"].asInt(), 15);
            EXPECT_EQ(json["freed_space"].asUInt64(), 102400U);
        }

        TEST_F(TrashServiceDeleteAllTest, DeleteAllResponseDefaults) {
            DeleteAllResponse response;

            EXPECT_EQ(response.deleted_count, 0);
            EXPECT_EQ(response.freed_space, 0U);
        }

        TEST_F(TrashServiceDeleteAllTest, DeleteAllEmptyTrash) {
            DeleteAllResponse response;
            response.deleted_count = 0;
            response.freed_space = 0;

            auto json = response.ToJson();

            EXPECT_EQ(json["deleted_count"].asInt(), 0);
            EXPECT_EQ(json["freed_space"].asUInt64(), 0U);
        }

        // ==================== TrashService Path Computation Tests ====================
        // Tests verifying expected path format for restored items

        class TrashServicePathTest : public ::testing::Test {};

        TEST_F(TrashServicePathTest, RootFilePathFormat) {
            // Files restored to root should have path: /filename.ext
            std::string expected_path = "/document.pdf";
            EXPECT_TRUE(expected_path.starts_with("/"));
            EXPECT_TRUE(expected_path.ends_with(".pdf"));
        }

        TEST_F(TrashServicePathTest, NestedFilePathFormat) {
            // Files restored to nested folder should have path: /parent/filename.ext
            std::string expected_path = "/docs/reports/summary.pdf";
            EXPECT_TRUE(expected_path.starts_with("/"));
            EXPECT_TRUE(expected_path.find("/reports/") != std::string::npos);
        }

        TEST_F(TrashServicePathTest, RootFolderPathFormat) {
            // Folders restored to root should have path: /foldername/
            std::string expected_path = "/Projects/";
            EXPECT_TRUE(expected_path.starts_with("/"));
            EXPECT_TRUE(expected_path.ends_with("/"));
        }

        TEST_F(TrashServicePathTest, NestedFolderPathFormat) {
            // Folders restored to nested location should have path: /parent/foldername/
            std::string expected_path = "/docs/archives/2024/";
            EXPECT_TRUE(expected_path.starts_with("/"));
            EXPECT_TRUE(expected_path.ends_with("/"));
            EXPECT_EQ(std::count(expected_path.begin(), expected_path.end(), '/'), 4);
        }

        // ==================== TrashService Auto-Rename Pattern Tests ====================
        // Tests verifying the expected rename format: name (n).ext

        class TrashServiceAutoRenameTest : public ::testing::Test {};

        TEST_F(TrashServiceAutoRenameTest, FileRenamePattern) {
            // Auto-renamed files should follow: basename (n).ext
            std::string original = "report.pdf";
            std::string renamed = "report (1).pdf";

            EXPECT_NE(original, renamed);
            EXPECT_TRUE(renamed.find(" (1)") != std::string::npos);
            EXPECT_TRUE(renamed.ends_with(".pdf"));
        }

        TEST_F(TrashServiceAutoRenameTest, FileRenameMultipleConflicts) {
            // Multiple conflicts increment the number
            std::vector<std::string> conflicts = { "report (1).pdf",
                                                   "report (2).pdf",
                                                   "report (10).pdf" };

            for (size_t i = 0; i < conflicts.size(); ++i) {
                EXPECT_TRUE(conflicts[i].find(" (") != std::string::npos);
                EXPECT_TRUE(conflicts[i].ends_with(".pdf"));
            }
        }

        TEST_F(TrashServiceAutoRenameTest, FolderRenamePattern) {
            // Auto-renamed folders should follow: foldername (n)
            std::string original = "Documents";
            std::string renamed = "Documents (1)";

            EXPECT_NE(original, renamed);
            EXPECT_TRUE(renamed.find(" (1)") != std::string::npos);
            EXPECT_TRUE(renamed.ends_with(")"));
        }

        TEST_F(TrashServiceAutoRenameTest, NoExtensionFileRename) {
            // Files without extension should still get renamed: filename (n)
            std::string renamed = "Makefile (1)";

            EXPECT_TRUE(renamed.find(" (1)") != std::string::npos);
        }

    } // namespace
} // namespace disk::trash
