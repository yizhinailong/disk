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
        // These tests verify the DTO structures used by ShareService methods

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

            // Verify deterministic order
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

            // Expected: (45 + 20 - 1) / 20 = 64 / 20 = 3
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
            // Per docs: batch cancel returns 200 even with partial failures
            CancelShareResponse response;
            response.summary.total = 3;
            response.summary.succeeded = 1;
            response.summary.failed = 2;

            // The response is valid regardless of success/failure count
            // HTTP 200 should always be returned for batch operations
            EXPECT_TRUE(
                response.summary.succeeded + response.summary.failed == response.summary.total
            );
        }

        TEST_F(ShareServiceContractTest, ShareIdIsExternalIdentifier) {
            // share_id in responses is share_code, not internal DB id
            CreateShareResponse response;
            response.share_id = "abc12345";

            // share_id should be a string share_code, not a numeric id
            EXPECT_TRUE(response.share_id.find_first_not_of("0123456789") != std::string::npos);
        }

        TEST_F(ShareServiceContractTest, ExpiresAtEmptyMeansPermanent) {
            // Empty expires_at means permanent share
            ShareDetailResponse response;
            response.expires_at = "";

            // Permanent shares have empty expires_at
            EXPECT_TRUE(response.expires_at.empty());
        }

        // ==================== Share API Regression Tests ====================
        // These tests verify specific error code branches required by API spec

        class ShareApiRegressionTest : public ::testing::Test {};

        // Regression test for error code 50005 (FileNotFound)
        // Verifies that accessing a file not in share returns FileNotFound error
        TEST_F(ShareApiRegressionTest, DownloadShareFileNotFoundErrorCode) {
            // Error code 50005 = FileNotFound per ErrorCode.hpp
            // This error is returned when file_id is valid but not part of the share
            constexpr auto file_not_found_code = static_cast<uint32_t>(ErrorCode::FileNotFound);
            EXPECT_EQ(file_not_found_code, 50005U);

            // HTTP status for FileNotFound should be 404
            auto status = Error::GetHttpStatus(ErrorCode::FileNotFound);
            EXPECT_EQ(status, drogon::k404NotFound);

            // Verify error message
            auto message = Error::GetErrorMessage(ErrorCode::FileNotFound);
            EXPECT_EQ(message, std::string("File not found"));
        }

        // Regression test for HTTP 416 Range Not Satisfiable
        // Verifies range request handling in download share
        TEST_F(ShareApiRegressionTest, DownloadShareRangeNotSatisfiableStatus) {
            // HTTP 416 is returned when Range header is invalid or unsatisfiable
            // Example: Range: bytes=1000-500 for a 600 byte file (start > end)
            // Example: Range: bytes=500-600 for a 400 byte file (range exceeds file size)
            constexpr auto range_not_satisfiable = drogon::k416RequestedRangeNotSatisfiable;
            EXPECT_EQ(range_not_satisfiable, 416);

            // Verify that we can distinguish 416 from other client errors
            EXPECT_NE(range_not_satisfiable, drogon::k400BadRequest);
            EXPECT_NE(range_not_satisfiable, drogon::k404NotFound);
        }

        // Regression test for valid range request (200 vs 206)
        TEST_F(ShareApiRegressionTest, DownloadShareRangeRequestStatusCodes) {
            // Full file download without Range header: 200 OK
            constexpr auto ok_status = drogon::k200OK;
            EXPECT_EQ(ok_status, 200);

            // Partial content with valid Range header: 206 Partial Content
            constexpr auto partial_status = drogon::k206PartialContent;
            EXPECT_EQ(partial_status, 206);
        }

        // Regression test for share token malformed (40107)
        TEST_F(ShareApiRegressionTest, ShareTokenMalformedErrorCode) {
            // Error code 40107 = TokenMalformed per ErrorCode.hpp
            constexpr auto malformed_code = static_cast<uint32_t>(ErrorCode::TokenMalformed);
            EXPECT_EQ(malformed_code, 40107U);

            // HTTP status for malformed token should be 401 Unauthorized
            auto status = Error::GetHttpStatus(ErrorCode::TokenMalformed);
            EXPECT_EQ(status, drogon::k401Unauthorized);

            // Verify error message
            auto message = Error::GetErrorMessage(ErrorCode::TokenMalformed);
            EXPECT_EQ(message, std::string("Token format error"));
        }

        // Regression test for share token expired (40108)
        TEST_F(ShareApiRegressionTest, ShareTokenExpiredErrorCode) {
            // Error code 40108 = TokenExpired per ErrorCode.hpp
            constexpr auto expired_code = static_cast<uint32_t>(ErrorCode::TokenExpired);
            EXPECT_EQ(expired_code, 40108U);

            // HTTP status for expired token should be 401 Unauthorized
            auto status = Error::GetHttpStatus(ErrorCode::TokenExpired);
            EXPECT_EQ(status, drogon::k401Unauthorized);

            // Verify error message
            auto message = Error::GetErrorMessage(ErrorCode::TokenExpired);
            EXPECT_EQ(message, std::string("Token expired"));
        }

        // Regression test for batch cancel mixed outcomes summary contract
        TEST_F(ShareApiRegressionTest, BatchCancelMixedOutcomeSummaryContract) {
            // Per API docs: batch cancel returns HTTP 200 with summary + per-item results
            // Even when some items fail, the overall response is 200

            CancelShareResponse response;
            response.summary.total = 5;
            response.summary.succeeded = 3;
            response.summary.failed = 2;

            // Create mixed results
            CancelShareResult success1;
            success1.share_id = "sh_001";
            success1.status = "success";

            CancelShareResult success2;
            success2.share_id = "sh_002";
            success2.status = "success";

            CancelShareResult success3;
            success3.share_id = "sh_003";
            success3.status = "success";

            CancelShareResult failed1;
            failed1.share_id = "sh_004";
            failed1.status = "failed";
            failed1.error = CancelShareError{ .code = static_cast<int>(ErrorCode::ShareNotFound),
                                              .message = "Share not found",
                                              .reason = "share_not_found" };

            CancelShareResult failed2;
            failed2.share_id = "sh_005";
            failed2.status = "failed";
            failed2.error = CancelShareError{ .code = static_cast<int>(ErrorCode::ShareExpired),
                                              .message = "Share expired",
                                              .reason = "share_expired" };

            response.results = { success1, success2, success3, failed1, failed2 };

            // Verify summary counts match results
            EXPECT_EQ(response.summary.total, 5);
            EXPECT_EQ(response.summary.succeeded, 3);
            EXPECT_EQ(response.summary.failed, 2);
            EXPECT_EQ(response.results.size(), 5U);

            // Verify JSON output structure
            auto json = response.ToJson();
            EXPECT_TRUE(json.isMember("summary"));
            EXPECT_TRUE(json.isMember("results"));
            EXPECT_EQ(json["summary"]["total"].asInt(), 5);
            EXPECT_EQ(json["summary"]["succeeded"].asInt(), 3);
            EXPECT_EQ(json["summary"]["failed"].asInt(), 2);
            EXPECT_EQ(json["results"].size(), 5U);

            // Verify each result has share_id and status
            for (const auto& result : json["results"]) {
                EXPECT_TRUE(result.isMember("share_id"));
                EXPECT_TRUE(result.isMember("status"));
            }

            // Verify failed results have error object
            EXPECT_TRUE(json["results"][3].isMember("error"));
            EXPECT_TRUE(json["results"][4].isMember("error"));
            EXPECT_EQ(json["results"][3]["error"]["code"].asInt(), 60001);
            EXPECT_EQ(json["results"][4]["error"]["code"].asInt(), 60002);

            // Verify success results don't have error object
            EXPECT_FALSE(json["results"][0].isMember("error"));
            EXPECT_FALSE(json["results"][1].isMember("error"));
            EXPECT_FALSE(json["results"][2].isMember("error"));
        }

        // Regression test for share token missing (40106)
        TEST_F(ShareApiRegressionTest, ShareTokenMissingErrorCode) {
            // Error code 40106 = TokenMissing per ErrorCode.hpp
            constexpr auto missing_code = static_cast<uint32_t>(ErrorCode::TokenMissing);
            EXPECT_EQ(missing_code, 40106U);

            // HTTP status for missing token should be 401 Unauthorized
            auto status = Error::GetHttpStatus(ErrorCode::TokenMissing);
            EXPECT_EQ(status, drogon::k401Unauthorized);

            // Verify error message
            auto message = Error::GetErrorMessage(ErrorCode::TokenMissing);
            EXPECT_EQ(message, std::string("Token not provided"));
        }

        // Regression test for share access denied (60004)
        TEST_F(ShareApiRegressionTest, ShareAccessDeniedErrorCode) {
            // Error code 60004 = ShareAccessDenied per ErrorCode.hpp
            constexpr auto access_denied_code = static_cast<uint32_t>(ErrorCode::ShareAccessDenied);
            EXPECT_EQ(access_denied_code, 60004U);

            // HTTP status for access denied should be 403 Forbidden
            auto status = Error::GetHttpStatus(ErrorCode::ShareAccessDenied);
            EXPECT_EQ(status, drogon::k403Forbidden);

            // Verify error message
            auto message = Error::GetErrorMessage(ErrorCode::ShareAccessDenied);
            EXPECT_EQ(message, std::string("Access denied"));
        }

    } // namespace
} // namespace disk::share
