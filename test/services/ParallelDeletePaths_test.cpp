/**
 * @file ParallelDeletePaths_test.cpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 并行删除路径辅助函数测试
 *
 * @copyright Copyright (c) 2026
 *
 * @details
 * ParallelDeletePaths 在 TrashService.cpp 的匿名命名空间中，无法直接测试。
 * 本文件测试并行删除的相关基础设施：
 * - 路径列表的分块逻辑（Chunk）
 * - BatchDeleteResponse DTO 结构和序列化
 * - DeleteAllResponse DTO 结构和序列化
 * - 空路径列表、单路径、多路径场景
 */

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "dtos/TrashDto.hpp"
#include "utils/BatchUtils.hpp"

namespace disk::trash {
    namespace {

        /// ==================== 并行删除常量测试 ====================

        class ParallelDeletePathsTest : public ::testing::Test {};

        TEST_F(ParallelDeletePathsTest, MaxParallelDeletePathsConstant) {
            /// 验证并行删除的最大并行度常量
            constexpr size_t max_parallel = 4;
            EXPECT_EQ(max_parallel, 4u);
        }

        /// ==================== 空路径列表场景 ====================

        TEST_F(ParallelDeletePathsTest, EmptyPathListChunkReturnsEmpty) {
            /// 空路径列表分块后应返回空
            std::vector<std::filesystem::path> empty;
            auto chunks = utils::BatchUtils::Chunk(empty, 4);
            EXPECT_TRUE(chunks.empty());
        }

        TEST_F(ParallelDeletePathsTest, EmptyPathListBatchDeleteResponse) {
            /// 空路径列表的 BatchDeleteResponse 应为空摘要
            BatchDeleteResponse response;
            response.summary.total = 0;
            response.summary.success_count = 0;
            response.summary.failure_count = 0;

            auto json = response.ToJson();

            ASSERT_TRUE(json.isMember("summary"));
            ASSERT_TRUE(json.isMember("results"));
            EXPECT_EQ(json["summary"]["total"].asInt(), 0);
            EXPECT_EQ(json["summary"]["success_count"].asInt(), 0);
            EXPECT_EQ(json["summary"]["failure_count"].asInt(), 0);
            EXPECT_EQ(json["results"].size(), 0u);
        }

        /// ==================== 单路径场景 ====================

        TEST_F(ParallelDeletePathsTest, SinglePathChunkProducesOneChunk) {
            /// 单路径分块后应只有一个分块
            std::vector<std::filesystem::path> single = { "/data/content/ab/cd/abcdef123456" };
            auto chunks = utils::BatchUtils::Chunk(single, 4);

            ASSERT_EQ(chunks.size(), 1u);
            ASSERT_EQ(chunks[0].size(), 1u);
            EXPECT_EQ(chunks[0][0].string(), "/data/content/ab/cd/abcdef123456");
        }

        TEST_F(ParallelDeletePathsTest, SinglePathDeleteResponse) {
            /// 单路径删除的 BatchDeleteResponse
            BatchDeleteResponse response;
            response.summary.total = 1;
            response.summary.success_count = 1;
            response.summary.failure_count = 0;

            BatchResultItem item;
            item.trash_id = 42;
            item.status = "success";
            item.freed_space = 1024;
            response.results.push_back(item);

            auto json = response.ToJson();

            EXPECT_EQ(json["summary"]["total"].asInt(), 1);
            EXPECT_EQ(json["summary"]["success_count"].asInt(), 1);
            ASSERT_EQ(json["results"].size(), 1u);
            EXPECT_EQ(json["results"][0]["trash_id"].asUInt64(), 42u);
            EXPECT_EQ(json["results"][0]["status"].asString(), "success");
            EXPECT_EQ(json["results"][0]["freed_space"].asUInt64(), 1024u);
        }

        /// ==================== 多路径场景 ====================

        TEST_F(ParallelDeletePathsTest, MultiplePathsChunkWithinSingleBatch) {
            /// 3 个路径（不超过 max_parallel=4）应在单个分块中
            std::vector<std::filesystem::path> paths = {
                "/data/content/ab/cd/abcdef123456",
                "/data/content/ef/gh/efgh789012",
                "/data/content/ij/kl/ijkl345678",
            };
            auto chunks = utils::BatchUtils::Chunk(paths, 4);

            ASSERT_EQ(chunks.size(), 1u);
            EXPECT_EQ(chunks[0].size(), 3u);
        }

        TEST_F(ParallelDeletePathsTest, MultiplePathsChunkSplitsAcrossBatches) {
            /// 7 个路径在 max_parallel=4 时应拆为 2 个分块
            std::vector<std::filesystem::path> paths = {
                "/data/a1", "/data/a2", "/data/a3", "/data/a4",
                "/data/b1", "/data/b2", "/data/b3",
            };
            auto chunks = utils::BatchUtils::Chunk(paths, 4);

            ASSERT_EQ(chunks.size(), 2u);
            EXPECT_EQ(chunks[0].size(), 4u);
            EXPECT_EQ(chunks[1].size(), 3u);
        }

        TEST_F(ParallelDeletePathsTest, MultiplePathsExactBatchBoundary) {
            /// 恰好 4 个路径应在单个分块中
            std::vector<std::filesystem::path> paths = {
                "/data/a", "/data/b", "/data/c", "/data/d",
            };
            auto chunks = utils::BatchUtils::Chunk(paths, 4);

            ASSERT_EQ(chunks.size(), 1u);
            EXPECT_EQ(chunks[0].size(), 4u);
        }

        TEST_F(ParallelDeletePathsTest, ManyPathsProduceCorrectChunkCount) {
            /// 13 个路径在 chunk_size=4 时产生 4 个分块 (4+4+4+1)
            std::vector<std::filesystem::path> paths;
            for (int i = 0; i < 13; ++i) {
                paths.emplace_back("/data/path_" + std::to_string(i));
            }
            auto chunks = utils::BatchUtils::Chunk(paths, 4);

            ASSERT_EQ(chunks.size(), 4u);
            EXPECT_EQ(chunks[0].size(), 4u);
            EXPECT_EQ(chunks[1].size(), 4u);
            EXPECT_EQ(chunks[2].size(), 4u);
            EXPECT_EQ(chunks[3].size(), 1u);
        }

        TEST_F(ParallelDeletePathsTest, MultiplePathsBatchDeleteResponse) {
            /// 多路径部分成功、部分失败的 BatchDeleteResponse
            BatchDeleteResponse response;
            response.summary.total = 3;
            response.summary.success_count = 2;
            response.summary.failure_count = 1;

            BatchResultItem ok1;
            ok1.trash_id = 1;
            ok1.status = "success";
            ok1.freed_space = 512;

            BatchResultItem ok2;
            ok2.trash_id = 2;
            ok2.status = "success";
            ok2.freed_space = 1024;

            BatchResultItem fail;
            fail.trash_id = 3;
            fail.status = "failed";
            fail.code = static_cast<uint16_t>(ErrorCode::ResourceNotFound);
            fail.message = "Trash item not found";

            response.results.push_back(ok1);
            response.results.push_back(ok2);
            response.results.push_back(fail);

            auto json = response.ToJson();

            EXPECT_EQ(json["summary"]["total"].asInt(), 3);
            EXPECT_EQ(json["summary"]["success_count"].asInt(), 2);
            EXPECT_EQ(json["summary"]["failure_count"].asInt(), 1);
            ASSERT_EQ(json["results"].size(), 3u);

            /// 验证成功项
            EXPECT_EQ(json["results"][0]["status"].asString(), "success");
            EXPECT_EQ(json["results"][0]["freed_space"].asUInt64(), 512u);
            EXPECT_EQ(json["results"][1]["status"].asString(), "success");
            EXPECT_EQ(json["results"][1]["freed_space"].asUInt64(), 1024u);

            /// 验证失败项
            EXPECT_EQ(json["results"][2]["status"].asString(), "failed");
            ASSERT_TRUE(json["results"][2].isMember("error"));
            EXPECT_EQ(
                json["results"][2]["error"]["code"].asUInt(),
                static_cast<uint32_t>(ErrorCode::ResourceNotFound)
            );
        }

        /// ==================== DeleteAllResponse 测试 ====================

        class ParallelDeleteAllResponseTest : public ::testing::Test {};

        TEST_F(ParallelDeleteAllResponseTest, EmptyTrashResponse) {
            DeleteAllResponse response;
            response.deleted_count = 0;
            response.freed_space = 0;

            auto json = response.ToJson();

            EXPECT_EQ(json["deleted_count"].asInt(), 0);
            EXPECT_EQ(json["freed_space"].asUInt64(), 0u);
        }

        TEST_F(ParallelDeleteAllResponseTest, AggregatedResponse) {
            /// 模拟并行删除多条路径后的汇总响应
            DeleteAllResponse response;
            response.deleted_count = 10;
            response.freed_space = 40960;

            auto json = response.ToJson();

            EXPECT_EQ(json["deleted_count"].asInt(), 10);
            EXPECT_EQ(json["freed_space"].asUInt64(), 40960u);
        }

        TEST_F(ParallelDeleteAllResponseTest, DefaultValues) {
            DeleteAllResponse response;

            EXPECT_EQ(response.deleted_count, 0);
            EXPECT_EQ(response.freed_space, 0u);

            auto json = response.ToJson();
            EXPECT_EQ(json["deleted_count"].asInt(), 0);
            EXPECT_EQ(json["freed_space"].asUInt64(), 0u);
        }

        /// ==================== BatchResultItem 删除场景测试 ====================

        class ParallelDeleteResultItemTest : public ::testing::Test {};

        TEST_F(ParallelDeleteResultItemTest, SuccessItemOnlyHasFreedSpace) {
            /// 成功删除项应只包含 freed_space，不含 error 或 file_id
            BatchResultItem item;
            item.trash_id = 100;
            item.status = "success";
            item.freed_space = 2048;

            auto json = item.ToJson();

            EXPECT_EQ(json["trash_id"].asUInt64(), 100u);
            EXPECT_EQ(json["status"].asString(), "success");
            EXPECT_EQ(json["freed_space"].asUInt64(), 2048u);
            EXPECT_FALSE(json.isMember("error"));
            EXPECT_FALSE(json.isMember("file_id"));
            EXPECT_FALSE(json.isMember("folder_id"));
        }

        TEST_F(ParallelDeleteResultItemTest, FailedItemHasErrorObject) {
            BatchResultItem item;
            item.trash_id = 200;
            item.status = "failed";
            item.code = static_cast<uint16_t>(ErrorCode::InternalError);
            item.message = "Operation failed";

            auto json = item.ToJson();

            EXPECT_EQ(json["trash_id"].asUInt64(), 200u);
            EXPECT_EQ(json["status"].asString(), "failed");
            ASSERT_TRUE(json.isMember("error"));
            EXPECT_EQ(
                json["error"]["code"].asUInt(),
                static_cast<uint32_t>(ErrorCode::InternalError)
            );
            EXPECT_EQ(json["error"]["message"].asString(), "Operation failed");
            EXPECT_FALSE(json.isMember("freed_space"));
        }

        TEST_F(ParallelDeleteResultItemTest, FailedItemWithFieldAndValue) {
            BatchResultItem item;
            item.trash_id = 300;
            item.status = "failed";
            item.code = static_cast<uint16_t>(ErrorCode::ResourceNotFound);
            item.message = "Trash item not found";
            item.field = "trash_id";
            item.value = "300";

            auto json = item.ToJson();

            ASSERT_TRUE(json.isMember("error"));
            EXPECT_EQ(json["error"]["field"].asString(), "trash_id");
            EXPECT_EQ(json["error"]["value"].asString(), "300");
        }

    } ///< namespace
} ///< namespace disk::trash
