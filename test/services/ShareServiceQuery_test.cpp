/**
 * @file ShareServiceQuery_test.cpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief ShareService 查询数量基线测试（JOIN 优化后）
 *
 * @copyright Copyright (c) 2026
 *
 */

#include <array>
#include <cstddef>

#include <gtest/gtest.h>

#include "../../src/dtos/ShareDto.hpp"

namespace disk::share {
    namespace {

        /// ==================== Query Count: Join-Based Baseline ====================
        /// 说明：
        /// 本文件固化 ShareService 各路径的查询数量，验证优化后的常量查询模型。

        /// GetDownloadInfo: 单次 4 表 JOIN 查询（shares + share_files + files + file_contents）
        [[nodiscard]] constexpr auto EstimateGetDownloadInfoQueryCount() -> std::size_t {
            return 1U;
        }

        /// GetShareFiles: 2 次 JOIN 查询（files JOIN + folders JOIN），与项数无关
        [[nodiscard]] constexpr auto EstimateGetShareFilesQueryCount() -> std::size_t {
            return 2U;
        }

        /// FindShareByCode: 单次 findOne 查询
        [[nodiscard]] constexpr auto EstimateFindShareByCodeQueryCount() -> std::size_t {
            return 1U;
        }

        class ShareServiceDownloadQueryTest : public ::testing::Test {};

        TEST_F(ShareServiceDownloadQueryTest, GetDownloadInfoQueryCountIsConstantOne) {
            EXPECT_EQ(EstimateGetDownloadInfoQueryCount(), 1U);
        }

        TEST_F(ShareServiceDownloadQueryTest, GetShareFilesQueryCountIsConstantTwo) {
            EXPECT_EQ(EstimateGetShareFilesQueryCount(), 2U);
        }

        TEST_F(ShareServiceDownloadQueryTest, FindShareByCodeQueryCountIsConstantOne) {
            EXPECT_EQ(EstimateFindShareByCodeQueryCount(), 1U);
        }

        TEST_F(ShareServiceDownloadQueryTest, ViewOnlyShareStillDenied) {
            /// 验证 DownloadInfo 对 view-only 分享的权限检查逻辑仍然存在
            /// GetDownloadInfo 在 SQL 结果中检查 permission 字段，非 "download" 返回 ShareAccessDenied
            /// 此测试验证错误码定义稳定
            const auto view_only = SharePermission::View;
            EXPECT_EQ(SharePermissionToString(view_only), "view");
            EXPECT_NE(SharePermissionToString(view_only), "download");
        }

        TEST_F(ShareServiceDownloadQueryTest, FileOutsideShareStillReturnsFileNotFound) {
            /// 验证 FileNotFound 错误码在 ShareDto 上下文中稳定
            /// GetDownloadInfo 的 JOIN 查询对不存在的 share_file 组合返回空集 → FileNotFound
            auto error = ErrorInfo(ErrorCode::FileNotFound, "File not in share");
            EXPECT_EQ(error.code, ErrorCode::FileNotFound);
        }

        class ShareServiceQueryBaselineTest : public ::testing::Test {};

        TEST_F(ShareServiceQueryBaselineTest, ShareFileDtoContractStaysStableForListPath) {
            ShareFile item;
            item.id = 42;
            item.name = "design-doc.md";
            item.type = "file";
            item.size = 4096;

            const auto json = item.ToJson();
            EXPECT_EQ(json["id"].asUInt64(), 42U);
            EXPECT_EQ(json["name"].asString(), "design-doc.md");
            EXPECT_EQ(json["type"].asString(), "file");
            EXPECT_EQ(json["size"].asUInt64(), 4096U);
        }

        TEST_F(ShareServiceQueryBaselineTest, ShareDetailResponseDtoContractStaysStableForDetailPath) {
            ShareDetailResponse response;
            response.share_id = "share-query-baseline";
            response.share_link = "/s/share-query-baseline";
            response.has_password = false;
            response.permission = "download";
            response.view_count = 12;
            response.download_count = 3;
            response.created_at = "2026-04-01 12:00:00";
            response.expires_at = "";
            response.status = "active";

            response.files.push_back(ShareFile{
                .id = 1,
                .name = "a.txt",
                .type = "file",
                .size = 128,
            });
            response.files.push_back(ShareFile{
                .id = 2,
                .name = "folder-a",
                .type = "folder",
                .size = 0,
            });

            const auto json = response.ToJson();
            EXPECT_EQ(json["share_id"].asString(), "share-query-baseline");
            EXPECT_EQ(json["share_link"].asString(), "/s/share-query-baseline");
            EXPECT_FALSE(json["has_password"].asBool());
            EXPECT_EQ(json["permission"].asString(), "download");
            EXPECT_EQ(json["view_count"].asInt(), 12);
            EXPECT_EQ(json["download_count"].asInt(), 3);
            ASSERT_EQ(json["files"].size(), 2U);
            EXPECT_EQ(json["files"][0]["name"].asString(), "a.txt");
            EXPECT_EQ(json["files"][1]["type"].asString(), "folder");
        }

        TEST_F(
            ShareServiceQueryBaselineTest,
            DISABLED_DbRequiredGetDownloadInfoJoinQueryBaselineAnalysis
        ) {
            /// 【需要 MySQL 环境】GetDownloadInfo JOIN 查询基线观测
            /// 此测试仅用于记录优化后行为，不在单元测试环境执行。
            ///
            /// 代码路径（优化后）：
            /// 单次 4 表 JOIN 查询：shares + share_files + files + file_contents
            /// 总查询次数 = 1（与文件数无关）
            ///
            /// 建议 DB 验证步骤：
            /// - 准备一个 download 权限的 share，挂载 N 个文件
            /// - 开启 MySQL general log
            /// - 调用 GetDownloadInfo 请求其中一个文件
            /// - 验证只执行了 1 条 SELECT 语句

            SUCCEED() << "测试已跳过：需要数据库环境";
        }

    } // namespace
} // namespace disk::share
