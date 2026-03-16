/**
 * @file ShareService_test.cpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief ShareService 单元测试
 *
 * @copyright Copyright (c) 2026
 *
 */

#include <gtest/gtest.h>

#include "dtos/ShareDto.hpp"

namespace disk::share {
    namespace {

        // ==================== ShareService Response DTO Tests ====================
        // 这些测试验证 ShareService 方法使用的 DTO 结构

        class ShareServiceResponseDtoTest : public ::testing::Test {};

        TEST_F(ShareServiceResponseDtoTest, CreateShareResponseFormat) {
            CreateShareResponse response;
            response.share_id = "abc12345";
            response.share_link = "/s/abc12345";
            response.password = "test1234";
            response.permission = "download";
            response.expires_at = "2026-03-15 10:00:00";
            response.created_at = "2026-02-15 10:00:00";

            auto json = response.ToJson();

            EXPECT_EQ(json["share_id"].asString(), "abc12345");
            EXPECT_EQ(json["share_link"].asString(), "/s/abc12345");
            EXPECT_EQ(json["password"].asString(), "test1234");
            EXPECT_EQ(json["permission"].asString(), "download");
            EXPECT_EQ(json["expires_at"].asString(), "2026-03-15 10:00:00");
            EXPECT_EQ(json["created_at"].asString(), "2026-02-15 10:00:00");
        }

        TEST_F(ShareServiceResponseDtoTest, CreateShareResponseWithoutPassword) {
            CreateShareResponse response;
            response.share_id = "xyz98765";
            response.share_link = "/s/xyz98765";
            response.permission = "view";
            response.expires_at = "";
            response.created_at = "2026-02-15 10:00:00";

            auto json = response.ToJson();

            EXPECT_EQ(json["share_id"].asString(), "xyz98765");
            EXPECT_FALSE(json.isMember("password"));
            EXPECT_EQ(json["permission"].asString(), "view");
            EXPECT_EQ(json["expires_at"].asString(), "");
        }

        TEST_F(ShareServiceResponseDtoTest, ShareItemFormat) {
            ShareItem item;
            item.share_id = "share001";
            item.file_name = "document.pdf";
            item.file_count = 3;
            item.share_link = "/s/share001";
            item.has_password = true;
            item.permission = "download";
            item.view_count = 100;
            item.download_count = 50;
            item.created_at = "2026-02-15 10:00:00";
            item.expires_at = "2026-03-15 10:00:00";
            item.status = "active";

            auto json = item.ToJson();

            EXPECT_EQ(json["share_id"].asString(), "share001");
            EXPECT_EQ(json["file_name"].asString(), "document.pdf");
            EXPECT_EQ(json["file_count"].asInt(), 3);
            EXPECT_EQ(json["has_password"].asBool(), true);
            EXPECT_EQ(json["permission"].asString(), "download");
            EXPECT_EQ(json["view_count"].asInt(), 100);
            EXPECT_EQ(json["download_count"].asInt(), 50);
            EXPECT_EQ(json["status"].asString(), "active");
        }

        TEST_F(ShareServiceResponseDtoTest, ShareListResponseWithPagination) {
            ShareListResponse response;

            ShareItem item1;
            item1.share_id = "share001";
            item1.file_name = "file1.pdf";
            item1.file_count = 1;
            item1.share_link = "/s/share001";
            item1.has_password = false;
            item1.permission = "view";
            item1.view_count = 10;
            item1.download_count = 0;
            item1.created_at = "2026-02-15 10:00:00";
            item1.expires_at = "2026-03-15 10:00:00";
            item1.status = "active";

            ShareItem item2;
            item2.share_id = "share002";
            item2.file_name = "file2.pdf";
            item2.file_count = 2;
            item2.share_link = "/s/share002";
            item2.has_password = true;
            item2.permission = "download";
            item2.view_count = 20;
            item2.download_count = 5;
            item2.created_at = "2026-02-14 10:00:00";
            item2.expires_at = "";
            item2.status = "cancelled";

            response.items.push_back(item1);
            response.items.push_back(item2);

            response.pagination.page = 1;
            response.pagination.page_size = 20;
            response.pagination.total = 45;
            response.pagination.total_pages = 3;

            auto json = response.ToJson();

            ASSERT_TRUE(json.isMember("items"));
            ASSERT_TRUE(json.isMember("pagination"));
            EXPECT_EQ(json["items"].size(), 2U);
            EXPECT_EQ(json["pagination"]["page"].asInt(), 1);
            EXPECT_EQ(json["pagination"]["page_size"].asInt(), 20);
            EXPECT_EQ(json["pagination"]["total"].asInt(), 45);
            EXPECT_EQ(json["pagination"]["total_pages"].asInt(), 3);
        }

        TEST_F(ShareServiceResponseDtoTest, ShareDetailResponseFormat) {
            ShareDetailResponse response;
            response.share_id = "detail001";
            response.share_link = "/s/detail001";
            response.has_password = true;
            response.permission = "download";
            response.view_count = 150;
            response.download_count = 75;
            response.created_at = "2026-02-15 10:00:00";
            response.expires_at = "2026-03-15 10:00:00";
            response.status = "active";

            ShareFile file1;
            file1.id = 1;
            file1.name = "report.pdf";
            file1.type = "file";
            file1.size = 1024000;

            ShareFile file2;
            file2.id = 2;
            file2.name = "data.xlsx";
            file2.type = "file";
            file2.size = 512000;

            response.files.push_back(file1);
            response.files.push_back(file2);

            auto json = response.ToJson();

            EXPECT_EQ(json["share_id"].asString(), "detail001");
            EXPECT_EQ(json["has_password"].asBool(), true);
            EXPECT_EQ(json["view_count"].asInt(), 150);
            EXPECT_EQ(json["files"].size(), 2U);
            EXPECT_EQ(json["files"][0]["name"].asString(), "report.pdf");
            EXPECT_EQ(json["files"][1]["name"].asString(), "data.xlsx");
        }

        TEST_F(ShareServiceResponseDtoTest, UpdateShareResponseFormat) {
            UpdateShareResponse response;
            response.share_id = "update001";
            response.expires_at = "2026-04-15 10:00:00";
            response.has_password = false;
            response.permission = "view";
            response.updated_at = "2026-02-15 12:00:00";

            auto json = response.ToJson();

            EXPECT_EQ(json["share_id"].asString(), "update001");
            EXPECT_EQ(json["expires_at"].asString(), "2026-04-15 10:00:00");
            EXPECT_EQ(json["has_password"].asBool(), false);
            EXPECT_EQ(json["permission"].asString(), "view");
            EXPECT_EQ(json["updated_at"].asString(), "2026-02-15 12:00:00");
        }

        // ==================== Cancel Share Response Tests ====================

        class ShareServiceCancelTest : public ::testing::Test {};

        TEST_F(ShareServiceCancelTest, CancelShareSummaryFormat) {
            CancelShareSummary summary;
            summary.total = 5;
            summary.succeeded = 3;
            summary.failed = 2;

            auto json = summary.ToJson();

            EXPECT_EQ(json["total"].asInt(), 5);
            EXPECT_EQ(json["succeeded"].asInt(), 3);
            EXPECT_EQ(json["failed"].asInt(), 2);
        }

        TEST_F(ShareServiceCancelTest, CancelShareResultSuccessFormat) {
            CancelShareResult result;
            result.share_id = "cancel001";
            result.status = "success";

            auto json = result.ToJson();

            EXPECT_EQ(json["share_id"].asString(), "cancel001");
            EXPECT_EQ(json["status"].asString(), "success");
            EXPECT_FALSE(json.isMember("error"));
        }

        TEST_F(ShareServiceCancelTest, CancelShareResultFailedFormat) {
            CancelShareResult result;
            result.share_id = "cancel002";
            result.status = "failed";
            result.error = CancelShareError{ .code = static_cast<int>(ErrorCode::ShareNotFound),
                                             .message = "Share not found",
                                             .reason = "share_not_found" };

            auto json = result.ToJson();

            EXPECT_EQ(json["share_id"].asString(), "cancel002");
            EXPECT_EQ(json["status"].asString(), "failed");
            ASSERT_TRUE(json.isMember("error"));
            EXPECT_EQ(json["error"]["code"].asInt(), static_cast<int>(ErrorCode::ShareNotFound));
            EXPECT_EQ(json["error"]["message"].asString(), "Share not found");
            EXPECT_EQ(json["error"]["reason"].asString(), "share_not_found");
        }

        TEST_F(ShareServiceCancelTest, CancelShareResponseMixedResult) {
            CancelShareResponse response;
            response.summary.total = 3;
            response.summary.succeeded = 2;
            response.summary.failed = 1;

            CancelShareResult success1;
            success1.share_id = "share001";
            success1.status = "success";

            CancelShareResult success2;
            success2.share_id = "share002";
            success2.status = "success";

            CancelShareResult failed;
            failed.share_id = "share003";
            failed.status = "failed";
            failed.error = CancelShareError{ .code = static_cast<int>(ErrorCode::ShareAccessDenied),
                                             .message = "Access denied",
                                             .reason = "access_denied" };

            response.results.push_back(success1);
            response.results.push_back(success2);
            response.results.push_back(failed);

            auto json = response.ToJson();

            ASSERT_TRUE(json.isMember("summary"));
            ASSERT_TRUE(json.isMember("results"));
            EXPECT_EQ(json["results"].size(), 3U);
            EXPECT_EQ(json["summary"]["succeeded"].asInt(), 2);
            EXPECT_EQ(json["summary"]["failed"].asInt(), 1);

            // 验证确定性顺序
            EXPECT_EQ(json["results"][0]["status"].asString(), "success");
            EXPECT_EQ(json["results"][1]["status"].asString(), "success");
            EXPECT_EQ(json["results"][2]["status"].asString(), "failed");
        }

        // ==================== Access Share Response Tests ====================

        class ShareServiceAccessTest : public ::testing::Test {};

        TEST_F(ShareServiceAccessTest, AccessShareResponseFormat) {
            AccessShareResponse response;
            response.share_token = "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9...";
            response.expires_in = 3600;
            response.permission = "download";

            ShareFile file1;
            file1.id = 1;
            file1.name = "shared_file.pdf";
            file1.type = "file";
            file1.size = 2048000;

            response.files.push_back(file1);

            auto json = response.ToJson();

            EXPECT_EQ(json["share_token"].asString(), "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9...");
            EXPECT_EQ(json["expires_in"].asInt(), 3600);
            EXPECT_EQ(json["permission"].asString(), "download");
            EXPECT_EQ(json["files"].size(), 1U);
            EXPECT_EQ(json["files"][0]["name"].asString(), "shared_file.pdf");
        }

        // ==================== Browse Share Response Tests ====================

        class ShareServiceBrowseTest : public ::testing::Test {};

        TEST_F(ShareServiceBrowseTest, BrowseShareResponseFormat) {
            BrowseShareResponse response;

            BrowseItem item1;
            item1.id = 1;
            item1.name = "folder1";
            item1.type = "folder";
            item1.size = 0;

            BrowseItem item2;
            item2.id = 2;
            item2.name = "file1.txt";
            item2.type = "file";
            item2.size = 1024;

            response.items.push_back(item1);
            response.items.push_back(item2);

            BrowseBreadcrumb breadcrumb;
            breadcrumb.id = 0;
            breadcrumb.name = "root";
            response.breadcrumb.push_back(breadcrumb);

            auto json = response.ToJson();

            ASSERT_TRUE(json.isMember("items"));
            ASSERT_TRUE(json.isMember("breadcrumb"));
            EXPECT_EQ(json["items"].size(), 2U);
            EXPECT_EQ(json["breadcrumb"].size(), 1U);
            EXPECT_EQ(json["items"][0]["type"].asString(), "folder");
            EXPECT_EQ(json["items"][1]["type"].asString(), "file");
        }

        TEST_F(ShareServiceBrowseTest, BrowseBreadcrumbFormat) {
            BrowseBreadcrumb crumb;
            crumb.id = 123;
            crumb.name = "Documents";

            auto json = crumb.ToJson();

            EXPECT_EQ(json["id"].asUInt64(), 123U);
            EXPECT_EQ(json["name"].asString(), "Documents");
        }

        // ==================== Share Status Enum Tests ====================

        class ShareServiceStatusTest : public ::testing::Test {};

        TEST_F(ShareServiceStatusTest, ShareStatusToStringActive) {
            EXPECT_EQ(ShareStatusToString(ShareStatus::Active), "active");
        }

        TEST_F(ShareServiceStatusTest, ShareStatusToStringExpired) {
            EXPECT_EQ(ShareStatusToString(ShareStatus::Expired), "expired");
        }

        TEST_F(ShareServiceStatusTest, ShareStatusToStringCancelled) {
            EXPECT_EQ(ShareStatusToString(ShareStatus::Cancelled), "cancelled");
        }

        // ==================== Share Permission Enum Tests ====================

        class ShareServicePermissionTest : public ::testing::Test {};

        TEST_F(ShareServicePermissionTest, SharePermissionToStringView) {
            EXPECT_EQ(SharePermissionToString(SharePermission::View), "view");
        }

        TEST_F(ShareServicePermissionTest, SharePermissionToStringDownload) {
            EXPECT_EQ(SharePermissionToString(SharePermission::Download), "download");
        }

        TEST_F(ShareServicePermissionTest, StringToSharePermissionValid) {
            auto view_opt = StringToSharePermission("view");
            ASSERT_TRUE(view_opt.has_value());
            EXPECT_EQ(*view_opt, SharePermission::View);

            auto download_opt = StringToSharePermission("download");
            ASSERT_TRUE(download_opt.has_value());
            EXPECT_EQ(*download_opt, SharePermission::Download);
        }

        TEST_F(ShareServicePermissionTest, StringToSharePermissionInvalid) {
            auto invalid_opt = StringToSharePermission("invalid");
            EXPECT_FALSE(invalid_opt.has_value());

            auto empty_opt = StringToSharePermission("");
            EXPECT_FALSE(empty_opt.has_value());
        }

        // ==================== ShareFile Tests ====================

        class ShareServiceFileTest : public ::testing::Test {};

        TEST_F(ShareServiceFileTest, ShareFileFormat) {
            ShareFile file;
            file.id = 42;
            file.name = "test_document.pdf";
            file.type = "file";
            file.size = 1234567;

            auto json = file.ToJson();

            EXPECT_EQ(json["id"].asUInt64(), 42U);
            EXPECT_EQ(json["name"].asString(), "test_document.pdf");
            EXPECT_EQ(json["type"].asString(), "file");
            EXPECT_EQ(json["size"].asUInt64(), 1234567U);
        }

        // ==================== Pagination Tests ====================

        class ShareServicePaginationTest : public ::testing::Test {};

        TEST_F(ShareServicePaginationTest, PaginationDefaultValues) {
            Pagination pagination;

            EXPECT_EQ(pagination.page, 1);
            EXPECT_EQ(pagination.page_size, 20);
            EXPECT_EQ(pagination.total, 0);
            EXPECT_EQ(pagination.total_pages, 0);
        }

        TEST_F(ShareServicePaginationTest, PaginationTotalPagesCalculation) {
            Pagination pagination;
            pagination.page = 1;
            pagination.page_size = 20;
            pagination.total = 45;

            // 预期：(45 + 20 - 1) / 20 = 64 / 20 = 3
            pagination.total_pages =
                pagination.page_size > 0 ?
                    (pagination.total + pagination.page_size - 1) / pagination.page_size :
                    0;

            EXPECT_EQ(pagination.total_pages, 3);
        }

        TEST_F(ShareServicePaginationTest, PaginationJsonFormat) {
            Pagination pagination;
            pagination.page = 2;
            pagination.page_size = 10;
            pagination.total = 95;
            pagination.total_pages = 10;

            auto json = pagination.ToJson();

            EXPECT_EQ(json["page"].asInt(), 2);
            EXPECT_EQ(json["page_size"].asInt(), 10);
            EXPECT_EQ(json["total"].asInt(), 95);
            EXPECT_EQ(json["total_pages"].asInt(), 10);
        }

        // ==================== Error Code Mapping Tests ====================

        class ShareServiceErrorCodeTest : public ::testing::Test {};

        TEST_F(ShareServiceErrorCodeTest, ShareNotFoundHttpStatus) {
            auto status = Error::GetHttpStatus(ErrorCode::ShareNotFound);
            EXPECT_EQ(status, drogon::k404NotFound);
        }

        TEST_F(ShareServiceErrorCodeTest, ShareExpiredHttpStatus) {
            auto status = Error::GetHttpStatus(ErrorCode::ShareExpired);
            EXPECT_EQ(status, drogon::k400BadRequest);
        }

        TEST_F(ShareServiceErrorCodeTest, SharePasswordErrorHttpStatus) {
            auto status = Error::GetHttpStatus(ErrorCode::SharePasswordError);
            EXPECT_EQ(status, drogon::k400BadRequest);
        }

        TEST_F(ShareServiceErrorCodeTest, ShareAccessDeniedHttpStatus) {
            auto status = Error::GetHttpStatus(ErrorCode::ShareAccessDenied);
            EXPECT_EQ(status, drogon::k403Forbidden);
        }

        // ==================== Contract Verification Tests ====================

        class ShareServiceContractTest : public ::testing::Test {};

        TEST_F(ShareServiceContractTest, CancelShareResponseAlwaysHttp200) {
            // 根据文档：批量取消即使有部分失败也返回 200
            // 无论成功/失败计数如何，响应均有效
            // 批量操作应始终返回 HTTP 200
            EXPECT_TRUE(
                response.summary.succeeded + response.summary.failed == response.summary.total
            );
        }

        TEST_F(ShareServiceContractTest, ShareIdIsExternalIdentifier) {
            // 响应中的 share_id 是 share_code，不是内部 DB id
            CreateShareResponse response;
            response.share_id = "abc12345";

            // share_id 应为字符串 share_code，不是数字 id
            EXPECT_TRUE(response.share_id.find_first_not_of("0123456789") != std::string::npos);
        }

        TEST_F(ShareServiceContractTest, ExpiresAtEmptyMeansPermanent) {
            // 空的 expires_at 表示永久分享
            ShareDetailResponse response;
            response.expires_at = "";

            // 永久分享的 expires_at 为空
            EXPECT_TRUE(response.expires_at.empty());
        }

        // ==================== Share API Regression Tests ====================
        // 这些测试验证 API 规范要求的特定错误码分支

        class ShareApiRegressionTest : public ::testing::Test {};

        // 错误码 50005 (FileNotFound) 的回归测试
        // 验证访问分享中不存在的文件返回 FileNotFound 错误
        // 错误码 50005 = FileNotFound（根据 ErrorCode.hpp）
        // 当 file_id 有效但不在分享中时返回此错误
        constexpr auto file_not_found_code = static_cast<uint32_t>(ErrorCode::FileNotFound);
        EXPECT_EQ(file_not_found_code, 50005U);

        // FileNotFound 的 HTTP 状态码应为 404
        auto status = Error::GetHttpStatus(ErrorCode::FileNotFound);
        EXPECT_EQ(status, drogon::k404NotFound);

        // 验证错误消息
        auto message = Error::GetErrorMessage(ErrorCode::FileNotFound);
        EXPECT_EQ(message, std::string("File not found"));
    }

    // HTTP 416 Range Not Satisfiable 的回归测试
    // 验证下载分享中的范围请求处理
    // 当 Range 头无效或无法满足时返回 HTTP 416
    // 示例：Range: bytes=1000-500 针对 600 字节文件（起始 > 结束）
    // 示例：Range: bytes=500-600 针对 400 字节文件（范围超出文件大小）
    constexpr auto range_not_satisfiable = drogon::k416RequestedRangeNotSatisfiable;
    EXPECT_EQ(range_not_satisfiable, 416);

    // 验证我们可以区分 416 与其他客户端错误
    EXPECT_NE(range_not_satisfiable, drogon::k400BadRequest);
    EXPECT_NE(range_not_satisfiable, drogon::k404NotFound);
}

// 有效范围请求（200 vs 206）的回归测试
// 不带 Range 头的完整文件下载：200 OK
// 带有效 Range 头的部分内容：206 Partial Content
constexpr auto partial_status = drogon::k206PartialContent;
EXPECT_EQ(partial_status, 206);
}

// 分享令牌格式错误（40107）的回归测试
// 错误码 40107 = TokenMalformed（根据 ErrorCode.hpp）
// 格式错误令牌的 HTTP 状态应为 401 Unauthorized
// 验证错误消息
auto message = Error::GetErrorMessage(ErrorCode::TokenMalformed);
EXPECT_EQ(message, std::string("Token format error"));
}

// 分享令牌过期（40108）的回归测试
// 错误码 40108 = TokenExpired（根据 ErrorCode.hpp）
// 过期令牌的 HTTP 状态应为 401 Unauthorized
// 验证错误消息
auto message = Error::GetErrorMessage(ErrorCode::TokenExpired);
EXPECT_EQ(message, std::string("Token expired"));
}

// 批量取消混合结果摘要契约的回归测试
// 根据 API 文档：批量取消返回 HTTP 200，带摘要 + 每项结果
// 即使某些项失败，总体响应仍为 200

CancelShareResponse response;
response.summary.total = 5;
response.summary.succeeded = 3;
response.summary.failed = 2;

// 创建混合结果
// 验证摘要计数与结果匹配
// 验证 JSON 输出结构
// 验证每个结果都有 share_id 和 status
// 验证失败结果有错误对象
// 验证成功结果没有错误对象
EXPECT_FALSE(json["results"][0].isMember("error"));
EXPECT_FALSE(json["results"][1].isMember("error"));
EXPECT_FALSE(json["results"][2].isMember("error"));
}

// 分享令牌缺失（40106）的回归测试
// 错误码 40106 = TokenMissing（根据 ErrorCode.hpp）
// 缺失令牌的 HTTP 状态应为 401 Unauthorized
// 验证错误消息
auto message = Error::GetErrorMessage(ErrorCode::TokenMissing);
EXPECT_EQ(message, std::string("Token not provided"));
}

// 分享访问被拒绝（60004）的回归测试
// 错误码 60004 = ShareAccessDenied（根据 ErrorCode.hpp）
// 访问被拒绝的 HTTP 状态应为 403 Forbidden
// 验证错误消息
auto message = Error::GetErrorMessage(ErrorCode::ShareAccessDenied);
EXPECT_EQ(message, std::string("Access denied"));
}

} // namespace
} // namespace disk::share
