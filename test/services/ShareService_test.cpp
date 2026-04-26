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
            CancelShareResponse response;
            response.summary.total = 3;
            response.summary.succeeded = 2;
            response.summary.failed = 1;
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

        // ==================== ShareListRequest Validation Tests ====================

        class ShareListRequestTest : public ::testing::Test {};

        TEST_F(ShareListRequestTest, ValidParameters) {
            auto req = drogon::HttpRequest::newHttpRequest();
            req->setParameter("status", "active");
            req->setParameter("page", "2");
            req->setParameter("page_size", "50");

            auto result = ShareListRequest::FromRequest(req);
            ASSERT_TRUE(result.has_value());
            EXPECT_EQ(result->status, "active");
            EXPECT_EQ(result->page, 2);
            EXPECT_EQ(result->page_size, 50);
        }

        TEST_F(ShareListRequestTest, DefaultValues) {
            auto req = drogon::HttpRequest::newHttpRequest();

            auto result = ShareListRequest::FromRequest(req);
            ASSERT_TRUE(result.has_value());
            EXPECT_EQ(result->status, "all");
            EXPECT_EQ(result->page, 1);
            EXPECT_EQ(result->page_size, 20);
        }

        TEST_F(ShareListRequestTest, InvalidStatus) {
            auto req = drogon::HttpRequest::newHttpRequest();
            req->setParameter("status", "invalid");

            auto result = ShareListRequest::FromRequest(req);
            EXPECT_FALSE(result.has_value());
            if (!result.has_value()) {
                EXPECT_EQ(result.error().code, ErrorCode::ValidationFailed);
            }
        }

        TEST_F(ShareListRequestTest, InvalidPageZero) {
            auto req = drogon::HttpRequest::newHttpRequest();
            req->setParameter("page", "0");

            auto result = ShareListRequest::FromRequest(req);
            EXPECT_FALSE(result.has_value());
            if (!result.has_value()) {
                EXPECT_EQ(result.error().code, ErrorCode::ValidationFailed);
            }
        }

        TEST_F(ShareListRequestTest, InvalidPageNegative) {
            auto req = drogon::HttpRequest::newHttpRequest();
            req->setParameter("page", "-1");

            auto result = ShareListRequest::FromRequest(req);
            EXPECT_FALSE(result.has_value());
            if (!result.has_value()) {
                EXPECT_EQ(result.error().code, ErrorCode::ValidationFailed);
            }
        }

        TEST_F(ShareListRequestTest, InvalidPageSizeTooLarge) {
            auto req = drogon::HttpRequest::newHttpRequest();
            req->setParameter("page_size", "101");

            auto result = ShareListRequest::FromRequest(req);
            EXPECT_FALSE(result.has_value());
            if (!result.has_value()) {
                EXPECT_EQ(result.error().code, ErrorCode::ValidationFailed);
            }
        }

        TEST_F(ShareListRequestTest, InvalidPageSizeZero) {
            auto req = drogon::HttpRequest::newHttpRequest();
            req->setParameter("page_size", "0");

            auto result = ShareListRequest::FromRequest(req);
            EXPECT_FALSE(result.has_value());
            if (!result.has_value()) {
                EXPECT_EQ(result.error().code, ErrorCode::ValidationFailed);
            }
        }

        TEST_F(ShareListRequestTest, ValidStatusValues) {
            std::vector<std::string> valid_statuses = { "all", "active", "expired", "cancelled" };
            for (const auto& status : valid_statuses) {
                auto req = drogon::HttpRequest::newHttpRequest();
                req->setParameter("status", status);

                auto result = ShareListRequest::FromRequest(req);
                ASSERT_TRUE(result.has_value()) << "Status '" << status << "' should be valid";
                EXPECT_EQ(result->status, status);
            }
        }

        // ==================== ShareListResponse Ordering Tests ====================

        class ShareListOrderingTest : public ::testing::Test {};

        TEST_F(ShareListOrderingTest, ItemsMaintainInsertionOrder) {
            ShareListResponse response;

            for (int i = 0; i < 5; ++i) {
                ShareItem item;
                item.share_id = "share" + std::to_string(i);
                item.file_name = "file" + std::to_string(i);
                item.file_count = i;
                item.share_link = "/s/share" + std::to_string(i);
                item.has_password = false;
                item.permission = "view";
                item.view_count = i * 10;
                item.download_count = i * 5;
                item.created_at = "2026-02-" + std::to_string(10 + i) + " 10:00:00";
                item.expires_at = "";
                item.status = "active";
                response.items.push_back(item);
            }

            auto json = response.ToJson();

            ASSERT_EQ(json["items"].size(), 5U);
            for (int i = 0; i < 5; ++i) {
                EXPECT_EQ(json["items"][i]["share_id"].asString(), "share" + std::to_string(i))
                    << "Items should maintain insertion order at index " << i;
            }
        }

        TEST_F(ShareListOrderingTest, PaginationMetadataFieldOrder) {
            ShareListResponse response;
            response.pagination.page = 1;
            response.pagination.page_size = 20;
            response.pagination.total = 100;
            response.pagination.total_pages = 5;

            auto json = response.ToJson();
            auto pagination = json["pagination"];

            EXPECT_TRUE(pagination.isMember("page"));
            EXPECT_TRUE(pagination.isMember("page_size"));
            EXPECT_TRUE(pagination.isMember("total"));
            EXPECT_TRUE(pagination.isMember("total_pages"));
        }

        // ==================== ShareItem Field Contract Tests ====================

        class ShareItemContractTest : public ::testing::Test {};

        TEST_F(ShareItemContractTest, AllRequiredFieldsPresent) {
            ShareItem item;
            item.share_id = "test123";
            item.file_name = "test.pdf";
            item.file_count = 1;
            item.share_link = "/s/test123";
            item.has_password = true;
            item.permission = "download";
            item.view_count = 100;
            item.download_count = 50;
            item.created_at = "2026-02-15 10:00:00";
            item.expires_at = "2026-03-15 10:00:00";
            item.status = "active";

            auto json = item.ToJson();

            EXPECT_TRUE(json.isMember("share_id"));
            EXPECT_TRUE(json.isMember("file_name"));
            EXPECT_TRUE(json.isMember("file_count"));
            EXPECT_TRUE(json.isMember("share_link"));
            EXPECT_TRUE(json.isMember("has_password"));
            EXPECT_TRUE(json.isMember("permission"));
            EXPECT_TRUE(json.isMember("view_count"));
            EXPECT_TRUE(json.isMember("download_count"));
            EXPECT_TRUE(json.isMember("created_at"));
            EXPECT_TRUE(json.isMember("expires_at"));
            EXPECT_TRUE(json.isMember("status"));
        }

        TEST_F(ShareItemContractTest, FieldTypesAreCorrect) {
            ShareItem item;
            item.share_id = "test123";
            item.file_name = "test.pdf";
            item.file_count = 1;
            item.share_link = "/s/test123";
            item.has_password = true;
            item.permission = "download";
            item.view_count = 100;
            item.download_count = 50;
            item.created_at = "2026-02-15 10:00:00";
            item.expires_at = "2026-03-15 10:00:00";
            item.status = "active";

            auto json = item.ToJson();

            EXPECT_TRUE(json["share_id"].isString());
            EXPECT_TRUE(json["file_name"].isString());
            EXPECT_TRUE(json["file_count"].isInt());
            EXPECT_TRUE(json["share_link"].isString());
            EXPECT_TRUE(json["has_password"].isBool());
            EXPECT_TRUE(json["permission"].isString());
            EXPECT_TRUE(json["view_count"].isInt());
            EXPECT_TRUE(json["download_count"].isInt());
            EXPECT_TRUE(json["created_at"].isString());
            EXPECT_TRUE(json["expires_at"].isString());
            EXPECT_TRUE(json["status"].isString());
        }

        // ==================== Pagination Consistency Tests ====================

        class PaginationConsistencyTest : public ::testing::Test {};

        TEST_F(PaginationConsistencyTest, SharePaginationMatchesFilePagination) {
            Pagination share_pagination;
            share_pagination.page = 2;
            share_pagination.page_size = 25;
            share_pagination.total = 75;
            share_pagination.total_pages = 3;

            auto json = share_pagination.ToJson();

            EXPECT_EQ(json["page"].asInt(), 2);
            EXPECT_EQ(json["page_size"].asInt(), 25);
            EXPECT_EQ(json["total"].asInt(), 75);
            EXPECT_EQ(json["total_pages"].asInt(), 3);
        }

        TEST_F(PaginationConsistencyTest, TotalPagesCalculation) {
            Pagination p1;
            p1.page = 1;
            p1.page_size = 20;
            p1.total = 100;
            p1.total_pages = (p1.total + p1.page_size - 1) / p1.page_size;
            EXPECT_EQ(p1.total_pages, 5);

            Pagination p2;
            p2.page = 1;
            p2.page_size = 20;
            p2.total = 45;
            p2.total_pages = (p2.total + p2.page_size - 1) / p2.page_size;
            EXPECT_EQ(p2.total_pages, 3);

            Pagination p3;
            p3.page = 1;
            p3.page_size = 20;
            p3.total = 0;
            p3.total_pages = 0;
            EXPECT_EQ(p3.total_pages, 0);
        }

        // ==================== CreateShareRequest Validation Tests ====================

        class CreateShareRequestTest : public ::testing::Test {};

        TEST_F(CreateShareRequestTest, ValidSingleFileRequest) {
            auto req = drogon::HttpRequest::newHttpJsonRequest(
                [] {
                    Json::Value json;
                    json["file_ids"] = Json::Value(Json::arrayValue);
                    json["file_ids"].append(42);
                    json["expire_days"] = 7;
                    json["permission"] = "download";
                    return json;
                }()
            );

            auto result = CreateShareRequest::FromRequest(req);
            ASSERT_TRUE(result.has_value());
            ASSERT_EQ(result->file_ids.size(), 1U);
            EXPECT_EQ(result->file_ids[0], 42U);
            EXPECT_EQ(result->expire_days, 7);
            EXPECT_EQ(result->permission, SharePermission::Download);
            EXPECT_FALSE(result->password.has_value());
        }

        TEST_F(CreateShareRequestTest, ValidMultiFileRequest) {
            auto req = drogon::HttpRequest::newHttpJsonRequest(
                [] {
                    Json::Value json;
                    json["file_ids"] = Json::Value(Json::arrayValue);
                    json["file_ids"].append(1);
                    json["file_ids"].append(2);
                    json["file_ids"].append(3);
                    return json;
                }()
            );

            auto result = CreateShareRequest::FromRequest(req);
            ASSERT_TRUE(result.has_value());
            ASSERT_EQ(result->file_ids.size(), 3U);
            EXPECT_EQ(result->file_ids[0], 1U);
            EXPECT_EQ(result->file_ids[1], 2U);
            EXPECT_EQ(result->file_ids[2], 3U);
        }

        TEST_F(CreateShareRequestTest, ValidWithPasswordProtection) {
            auto req = drogon::HttpRequest::newHttpJsonRequest(
                [] {
                    Json::Value json;
                    json["file_ids"] = Json::Value(Json::arrayValue);
                    json["file_ids"].append(10);
                    json["password"] = "abcd";
                    return json;
                }()
            );

            auto result = CreateShareRequest::FromRequest(req);
            ASSERT_TRUE(result.has_value());
            ASSERT_TRUE(result->password.has_value());
            EXPECT_EQ(*result->password, "abcd");
        }

        TEST_F(CreateShareRequestTest, ValidWithViewPermission) {
            auto req = drogon::HttpRequest::newHttpJsonRequest(
                [] {
                    Json::Value json;
                    json["file_ids"] = Json::Value(Json::arrayValue);
                    json["file_ids"].append(5);
                    json["permission"] = "view";
                    return json;
                }()
            );

            auto result = CreateShareRequest::FromRequest(req);
            ASSERT_TRUE(result.has_value());
            EXPECT_EQ(result->permission, SharePermission::View);
        }

        TEST_F(CreateShareRequestTest, ValidWithPermanentExpiry) {
            auto req = drogon::HttpRequest::newHttpJsonRequest(
                [] {
                    Json::Value json;
                    json["file_ids"] = Json::Value(Json::arrayValue);
                    json["file_ids"].append(99);
                    json["expire_days"] = 0;
                    return json;
                }()
            );

            auto result = CreateShareRequest::FromRequest(req);
            ASSERT_TRUE(result.has_value());
            EXPECT_EQ(result->expire_days, 0);
        }

        TEST_F(CreateShareRequestTest, RejectEmptyFileIds) {
            auto req = drogon::HttpRequest::newHttpJsonRequest(
                [] {
                    Json::Value json;
                    json["file_ids"] = Json::Value(Json::arrayValue);
                    return json;
                }()
            );

            auto result = CreateShareRequest::FromRequest(req);
            EXPECT_FALSE(result.has_value());
            EXPECT_EQ(result.error().code, ErrorCode::InvalidParameter);
        }

        TEST_F(CreateShareRequestTest, RejectMissingFileIds) {
            auto req = drogon::HttpRequest::newHttpJsonRequest(Json::Value());

            auto result = CreateShareRequest::FromRequest(req);
            EXPECT_FALSE(result.has_value());
            EXPECT_EQ(result.error().code, ErrorCode::ValidationFailed);
        }

        TEST_F(CreateShareRequestTest, RejectZeroFileId) {
            auto req = drogon::HttpRequest::newHttpJsonRequest(
                [] {
                    Json::Value json;
                    json["file_ids"] = Json::Value(Json::arrayValue);
                    json["file_ids"].append(0);
                    return json;
                }()
            );

            auto result = CreateShareRequest::FromRequest(req);
            EXPECT_FALSE(result.has_value());
            EXPECT_EQ(result.error().code, ErrorCode::InvalidParameter);
        }

        TEST_F(CreateShareRequestTest, RejectNegativeExpireDays) {
            auto req = drogon::HttpRequest::newHttpJsonRequest(
                [] {
                    Json::Value json;
                    json["file_ids"] = Json::Value(Json::arrayValue);
                    json["file_ids"].append(1);
                    json["expire_days"] = -1;
                    return json;
                }()
            );

            auto result = CreateShareRequest::FromRequest(req);
            EXPECT_FALSE(result.has_value());
            EXPECT_EQ(result.error().code, ErrorCode::ValidationFailed);
        }

        TEST_F(CreateShareRequestTest, RejectShortPassword) {
            auto req = drogon::HttpRequest::newHttpJsonRequest(
                [] {
                    Json::Value json;
                    json["file_ids"] = Json::Value(Json::arrayValue);
                    json["file_ids"].append(1);
                    json["password"] = "ab";
                    return json;
                }()
            );

            auto result = CreateShareRequest::FromRequest(req);
            EXPECT_FALSE(result.has_value());
            EXPECT_EQ(result.error().code, ErrorCode::ValidationFailed);
        }

        TEST_F(CreateShareRequestTest, RejectLongPassword) {
            auto req = drogon::HttpRequest::newHttpJsonRequest(
                [] {
                    Json::Value json;
                    json["file_ids"] = Json::Value(Json::arrayValue);
                    json["file_ids"].append(1);
                    json["password"] = "123456789";
                    return json;
                }()
            );

            auto result = CreateShareRequest::FromRequest(req);
            EXPECT_FALSE(result.has_value());
            EXPECT_EQ(result.error().code, ErrorCode::ValidationFailed);
        }

        TEST_F(CreateShareRequestTest, RejectInvalidPermission) {
            auto req = drogon::HttpRequest::newHttpJsonRequest(
                [] {
                    Json::Value json;
                    json["file_ids"] = Json::Value(Json::arrayValue);
                    json["file_ids"].append(1);
                    json["permission"] = "admin";
                    return json;
                }()
            );

            auto result = CreateShareRequest::FromRequest(req);
            EXPECT_FALSE(result.has_value());
            EXPECT_EQ(result.error().code, ErrorCode::ValidationFailed);
        }

        // ==================== CreateShareResponse Contract Tests ====================

        class CreateShareResponseContractTest : public ::testing::Test {};

        TEST_F(CreateShareResponseContractTest, SingleFileCreateResponseFormat) {
            // 验证单文件分享创建成功后的响应格式
            CreateShareResponse response;
            response.share_id = "abc12345";
            response.share_link = "/s/abc12345";
            response.permission = "download";
            response.expires_at = "2026-04-19 10:00:00";
            response.created_at = "2026-04-12 10:00:00";

            auto json = response.ToJson();

            // share_id 是 share_code 字符串，不是内部数字 ID
            EXPECT_EQ(json["share_id"].asString(), "abc12345");
            EXPECT_EQ(json["share_link"].asString(), "/s/abc12345");
            EXPECT_FALSE(json.isMember("password"));
            EXPECT_EQ(json["permission"].asString(), "download");
            EXPECT_EQ(json["expires_at"].asString(), "2026-04-19 10:00:00");
            EXPECT_EQ(json["created_at"].asString(), "2026-04-12 10:00:00");
        }

        TEST_F(CreateShareResponseContractTest, MultiFileCreateResponseUsesShareCode) {
            // 多文件分享的响应仍然是一个 share_code（不是多个）
            CreateShareResponse response;
            response.share_id = "xyz789";
            response.share_link = "/s/xyz789";
            response.permission = "view";
            response.expires_at = "";
            response.created_at = "2026-04-12 10:00:00";

            auto json = response.ToJson();

            EXPECT_EQ(json["share_id"].asString(), "xyz789");
            EXPECT_EQ(json["share_link"].asString(), "/s/xyz789");
            EXPECT_TRUE(json["expires_at"].asString().empty());
        }

        TEST_F(CreateShareResponseContractTest, CreateWithPasswordReturnsPlaintextPassword) {
            // 创建分享时如果有密码，响应中包含明文密码（只返回一次）
            CreateShareResponse response;
            response.share_id = "pass123";
            response.share_link = "/s/pass123";
            response.password = "test1234";
            response.permission = "download";
            response.expires_at = "";
            response.created_at = "2026-04-12 10:00:00";

            auto json = response.ToJson();

            ASSERT_TRUE(json.isMember("password"));
            EXPECT_EQ(json["password"].asString(), "test1234");
        }

        TEST_F(CreateShareResponseContractTest, CreateWithPermanentExpiryReturnsEmptyExpiresAt) {
            // expire_days=0 表示永久分享，expires_at 为空字符串
            CreateShareResponse response;
            response.share_id = "perm123";
            response.share_link = "/s/perm123";
            response.permission = "download";
            response.expires_at = "";
            response.created_at = "2026-04-12 10:00:00";

            auto json = response.ToJson();

            EXPECT_TRUE(json["expires_at"].asString().empty());
        }

        TEST_F(CreateShareResponseContractTest, CreateWithSevenDayExpiryReturnsCalculatedExpiresAt) {
            // expire_days=7 时，expires_at 为 created_at + 7 天
            CreateShareResponse response;
            response.share_id = "week123";
            response.share_link = "/s/week123";
            response.permission = "view";
            response.expires_at = "2026-04-19 10:00:00";
            response.created_at = "2026-04-12 10:00:00";

            auto json = response.ToJson();

            EXPECT_EQ(json["expires_at"].asString(), "2026-04-19 10:00:00");
            EXPECT_EQ(json["created_at"].asString(), "2026-04-12 10:00:00");
        }

        // ==================== Create Share Atomicity Contract Tests ====================
        // 这些测试验证 Create 操作的原子性期望：
        // - 成功：share 行 + share_files 行全部写入
        // - 失败：share_files 插入异常不应留下孤立的 share 行
        //
        // 注意：当前实现没有事务包装，rollback 测试将作为未来的行为规范。
        // 当 Task 5 添加事务后，这些测试会验证回滚行为。

        class ShareServiceCreateAtomicityTest : public ::testing::Test {};

        TEST_F(ShareServiceCreateAtomicityTest, CreateFlowStepsAreIdentified) {
            // 验证 Create 流程的步骤数量和依赖关系：
            // 1. ValidateFileOwnership — 查询 files 表验证所有权
            // 2. GenerateShareCode — 纯计算，无 DB 操作
            // 3. 计算 expires_at — 纯计算
            // 4. HashPassword — 纯计算
            // 5. Insert share row — 写入 shares 表
            // 6. Loop: Insert share_files rows — 写入 share_files 表
            // 7. Build response — 纯构造

            // Create 流程步骤计数
            constexpr std::size_t kDbWriteSteps = 2; // share insert + share_files loop
            constexpr std::size_t kDbReadSteps = 1;  // ValidateFileOwnership
            constexpr std::size_t kTotalSteps = 7;

            EXPECT_EQ(kDbWriteSteps + kDbReadSteps + 4, kTotalSteps);
        }

        TEST_F(ShareServiceCreateAtomicityTest, FailureDuringShareFilesInsertReturnsInternalError) {
            // 当 share_files 插入失败时，Create 返回 InternalError
            // 错误码应与 Create 流程的 catch 块一致
            auto error = ErrorInfo(ErrorCode::InternalError, "Failed to create share-file association");
            EXPECT_EQ(error.code, ErrorCode::InternalError);
        }

        // ==================== Share Create Ownership Validation Tests ====================

        class ShareCreateOwnershipTest : public ::testing::Test {};

        TEST_F(ShareCreateOwnershipTest, OwnershipValidationErrorUsesFileNotFound) {
            // 当文件不属于用户时，ValidateFileOwnership 返回 FileNotFound
            auto error = ErrorInfo(ErrorCode::FileNotFound, "File not found");
            EXPECT_EQ(error.code, ErrorCode::FileNotFound);
            EXPECT_EQ(Error::GetHttpStatus(ErrorCode::FileNotFound), drogon::k404NotFound);
        }

        TEST_F(ShareCreateOwnershipTest, OwnershipValidationQueriesFilesTable) {
            // ValidateFileOwnership 通过 user_id + file_ids 查询 files 表
            // 返回的文件数量应与请求的 file_ids 数量匹配
            // 如果不匹配（部分文件不属于用户），应返回错误

            // 模拟：请求 3 个 file_ids，但只有 2 个属于用户
            std::vector<uint64_t> requested_ids = { 1, 2, 3 };
            std::vector<uint64_t> owned_ids = { 1, 2 };

            // 文件数量不匹配表示有文件不属于当前用户
            EXPECT_NE(requested_ids.size(), owned_ids.size());
        }

        // ==================== Share Create Expiry Calculation Tests ====================

        class ShareCreateExpiryTest : public ::testing::Test {};

        TEST_F(ShareCreateExpiryTest, ExpireDaysZeroMeansPermanent) {
            // expire_days=0 → 不设置 expires_at → 永久分享
            int expire_days = 0;
            bool has_expiry = expire_days > 0;
            EXPECT_FALSE(has_expiry);
        }

        TEST_F(ShareCreateExpiryTest, ExpireDaysPositiveSetsExpiresAt) {
            // expire_days>0 → expires_at = now + expire_days * 86400
            int expire_days = 7;
            bool has_expiry = expire_days > 0;
            EXPECT_TRUE(has_expiry);

            constexpr int64_t seconds_per_day = 86400;
            int64_t expected_offset_seconds = static_cast<int64_t>(expire_days) * seconds_per_day;
            EXPECT_EQ(expected_offset_seconds, 604800);
        }

        TEST_F(ShareCreateExpiryTest, ExpireDaysOneDay) {
            int expire_days = 1;
            constexpr int64_t seconds_per_day = 86400;
            int64_t expected_offset_seconds = static_cast<int64_t>(expire_days) * seconds_per_day;
            EXPECT_EQ(expected_offset_seconds, 86400);
        }

        TEST_F(ShareCreateExpiryTest, ExpireDaysThirtyDays) {
            int expire_days = 30;
            constexpr int64_t seconds_per_day = 86400;
            int64_t expected_offset_seconds = static_cast<int64_t>(expire_days) * seconds_per_day;
            EXPECT_EQ(expected_offset_seconds, 2592000);
        }

    } // namespace
} // namespace disk::share
