/**
 * @file TrashDto_test.cpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief Trash DTO unit tests
 * @version 0.1
 * @date 2026-02-15
 *
 * @copyright Copyright (c) 2026
 *
 */

#include "dtos/TrashDto.hpp"

#include <string>

#include <drogon/HttpRequest.h>
#include <gtest/gtest.h>
#include <json/json.h>

#include "utils/ErrorCode.hpp"

using disk::trash::BatchDeleteResponse;
using disk::trash::BatchRestoreResponse;
using disk::trash::BatchResultItem;
using disk::trash::BatchSummary;
using disk::trash::DeleteAllResponse;
using disk::trash::TrashBatchRequest;
using disk::trash::TrashItemResponse;
using disk::trash::TrashListRequest;

// ==================== TrashListRequest Tests ====================

static auto CreateListRequest(const std::string& page, const std::string& page_size)
    -> drogon::HttpRequestPtr {
    auto req = drogon::HttpRequest::newHttpRequest();
    if (!page.empty()) {
        req->setParameter("page", page);
    }
    if (!page_size.empty()) {
        req->setParameter("page_size", page_size);
    }
    return req;
}

TEST(TrashListRequest, DefaultPagination) {
    auto req = CreateListRequest("", "");
    auto result = TrashListRequest::FromRequest(req);

    ASSERT_TRUE(result.has_value()) << "Default pagination should pass validation";
    EXPECT_EQ(result->page, 1);
    EXPECT_EQ(result->page_size, 20);
}

TEST(TrashListRequest, CustomPagination) {
    auto req = CreateListRequest("2", "50");
    auto result = TrashListRequest::FromRequest(req);

    ASSERT_TRUE(result.has_value()) << "Custom pagination should pass validation";
    EXPECT_EQ(result->page, 2);
    EXPECT_EQ(result->page_size, 50);
}

TEST(TrashListRequest, PageMinimumValue) {
    auto req = CreateListRequest("1", "10");
    auto result = TrashListRequest::FromRequest(req);

    ASSERT_TRUE(result.has_value()) << "Page=1 should pass validation";
    EXPECT_EQ(result->page, 1);
}

TEST(TrashListRequest, PageZeroInvalid) {
    auto req = CreateListRequest("0", "10");
    auto result = TrashListRequest::FromRequest(req);

    EXPECT_FALSE(result.has_value()) << "Page=0 should fail validation";
    if (!result.has_value()) {
        EXPECT_EQ(result.error().code, ErrorCode::ValidationFailed);
    }
}

TEST(TrashListRequest, PageNegativeInvalid) {
    auto req = CreateListRequest("-1", "10");
    auto result = TrashListRequest::FromRequest(req);

    EXPECT_FALSE(result.has_value()) << "Negative page should fail validation";
}

TEST(TrashListRequest, PageInvalidFormat) {
    auto req = CreateListRequest("abc", "10");
    auto result = TrashListRequest::FromRequest(req);

    EXPECT_FALSE(result.has_value()) << "Non-numeric page should fail validation";
}

TEST(TrashListRequest, PageSizeMinimum) {
    auto req = CreateListRequest("1", "1");
    auto result = TrashListRequest::FromRequest(req);

    ASSERT_TRUE(result.has_value()) << "page_size=1 should pass validation";
    EXPECT_EQ(result->page_size, 1);
}

TEST(TrashListRequest, PageSizeMaximum) {
    auto req = CreateListRequest("1", "100");
    auto result = TrashListRequest::FromRequest(req);

    ASSERT_TRUE(result.has_value()) << "page_size=100 should pass validation";
    EXPECT_EQ(result->page_size, 100);
}

TEST(TrashListRequest, PageSizeExceedsMaximum) {
    auto req = CreateListRequest("1", "101");
    auto result = TrashListRequest::FromRequest(req);

    EXPECT_FALSE(result.has_value()) << "page_size=101 should fail validation";
}

TEST(TrashListRequest, PageSizeZeroInvalid) {
    auto req = CreateListRequest("1", "0");
    auto result = TrashListRequest::FromRequest(req);

    EXPECT_FALSE(result.has_value()) << "page_size=0 should fail validation";
}

TEST(TrashListRequest, PageSizeInvalidFormat) {
    auto req = CreateListRequest("1", "abc");
    auto result = TrashListRequest::FromRequest(req);

    EXPECT_FALSE(result.has_value()) << "Non-numeric page_size should fail validation";
}

// ==================== TrashBatchRequest Tests ====================

static auto CreateBatchRequest(const std::string& body) -> drogon::HttpRequestPtr {
    auto req = drogon::HttpRequest::newHttpRequest();
    req->setBody(body);
    req->setContentTypeCode(drogon::CT_APPLICATION_JSON);
    return req;
}

static auto CreateBatchRequestWithIds(const std::vector<uint64_t>& ids) -> drogon::HttpRequestPtr {
    Json::Value json;
    Json::Value ids_array(Json::arrayValue);
    for (auto id : ids) {
        ids_array.append(static_cast<Json::UInt64>(id));
    }
    json["trash_ids"] = ids_array;

    Json::StreamWriterBuilder builder;
    builder["indentation"] = "";
    std::string body = Json::writeString(builder, json);

    return CreateBatchRequest(body);
}

TEST(TrashBatchRequest, ValidTrashIds) {
    auto req = CreateBatchRequestWithIds({ 1, 2, 3 });
    auto result = TrashBatchRequest::FromRequest(req);

    ASSERT_TRUE(result.has_value()) << "Valid trash_ids should pass validation";
    EXPECT_EQ(result->trash_ids.size(), 3);
    EXPECT_EQ(result->trash_ids[0], 1);
    EXPECT_EQ(result->trash_ids[1], 2);
    EXPECT_EQ(result->trash_ids[2], 3);
}

TEST(TrashBatchRequest, SingleId) {
    auto req = CreateBatchRequestWithIds({ 42 });
    auto result = TrashBatchRequest::FromRequest(req);

    ASSERT_TRUE(result.has_value()) << "Single trash_id should pass validation";
    EXPECT_EQ(result->trash_ids.size(), 1);
    EXPECT_EQ(result->trash_ids[0], 42);
}

TEST(TrashBatchRequest, MaxHundredIds) {
    std::vector<uint64_t> ids;
    for (int i = 1; i <= 100; ++i) {
        ids.push_back(static_cast<uint64_t>(i));
    }
    auto req = CreateBatchRequestWithIds(ids);
    auto result = TrashBatchRequest::FromRequest(req);

    ASSERT_TRUE(result.has_value()) << "100 trash_ids should pass validation";
    EXPECT_EQ(result->trash_ids.size(), 100);
}

TEST(TrashBatchRequest, ExceedsMaxHundredIds) {
    std::vector<uint64_t> ids;
    for (int i = 1; i <= 101; ++i) {
        ids.push_back(static_cast<uint64_t>(i));
    }
    auto req = CreateBatchRequestWithIds(ids);
    auto result = TrashBatchRequest::FromRequest(req);

    EXPECT_FALSE(result.has_value()) << "101 trash_ids should fail validation";
    if (!result.has_value()) {
        EXPECT_EQ(result.error().code, ErrorCode::ValidationFailed);
    }
}

TEST(TrashBatchRequest, EmptyTrashIds) {
    Json::Value json;
    json["trash_ids"] = Json::Value(Json::arrayValue);

    Json::StreamWriterBuilder builder;
    builder["indentation"] = "";
    std::string body = Json::writeString(builder, json);

    auto req = CreateBatchRequest(body);
    auto result = TrashBatchRequest::FromRequest(req);

    EXPECT_FALSE(result.has_value()) << "Empty trash_ids array should fail validation";
}

TEST(TrashBatchRequest, MissingTrashIds) {
    Json::Value json;

    Json::StreamWriterBuilder builder;
    builder["indentation"] = "";
    std::string body = Json::writeString(builder, json);

    auto req = CreateBatchRequest(body);
    auto result = TrashBatchRequest::FromRequest(req);

    EXPECT_FALSE(result.has_value()) << "Missing trash_ids should fail validation";
}

TEST(TrashBatchRequest, InvalidTrashIdsType) {
    Json::Value json;
    json["trash_ids"] = "not_an_array";

    Json::StreamWriterBuilder builder;
    builder["indentation"] = "";
    std::string body = Json::writeString(builder, json);

    auto req = CreateBatchRequest(body);
    auto result = TrashBatchRequest::FromRequest(req);

    EXPECT_FALSE(result.has_value()) << "Non-array trash_ids should fail validation";
}

TEST(TrashBatchRequest, InvalidElementString) {
    Json::Value json;
    Json::Value ids_array(Json::arrayValue);
    ids_array.append("not_a_number");
    ids_array.append(2);
    json["trash_ids"] = ids_array;

    Json::StreamWriterBuilder builder;
    builder["indentation"] = "";
    std::string body = Json::writeString(builder, json);

    auto req = CreateBatchRequest(body);
    auto result = TrashBatchRequest::FromRequest(req);

    EXPECT_FALSE(result.has_value()) << "String element in trash_ids should fail validation";
}

TEST(TrashBatchRequest, ZeroIdInvalid) {
    auto req = CreateBatchRequestWithIds({ 0, 1, 2 });
    auto result = TrashBatchRequest::FromRequest(req);

    EXPECT_FALSE(result.has_value()) << "Zero trash_id should fail validation";
}

TEST(TrashBatchRequest, InvalidJSON) {
    auto req = CreateBatchRequest("{invalid json}");
    auto result = TrashBatchRequest::FromRequest(req);

    EXPECT_FALSE(result.has_value()) << "Invalid JSON should fail validation";
}

TEST(TrashBatchRequest, FloatIdConverted) {
    Json::Value json;
    Json::Value ids_array(Json::arrayValue);
    ids_array.append(1.5);
    json["trash_ids"] = ids_array;

    Json::StreamWriterBuilder builder;
    builder["indentation"] = "";
    std::string body = Json::writeString(builder, json);

    auto req = CreateBatchRequest(body);
    auto result = TrashBatchRequest::FromRequest(req);

    EXPECT_FALSE(result.has_value()) << "Float element in trash_ids should fail validation";
}

// ==================== Response DTO Tests ====================

TEST(BatchSummary, ToJsonCorrectFields) {
    BatchSummary summary;
    summary.total = 10;
    summary.success_count = 7;
    summary.failure_count = 3;

    auto json = summary.ToJson();

    EXPECT_EQ(json["total"].asInt(), 10);
    EXPECT_EQ(json["success_count"].asInt(), 7);
    EXPECT_EQ(json["failure_count"].asInt(), 3);
}

TEST(BatchResultItem, RestoreSuccessFlatFields) {
    BatchResultItem item;
    item.trash_id = 123;
    item.status = "success";
    item.file_id = 456;
    item.path = "/documents/file.txt";

    auto json = item.ToJson();

    EXPECT_EQ(json["trash_id"].asUInt64(), 123);
    EXPECT_EQ(json["status"].asString(), "success");
    EXPECT_EQ(json["file_id"].asUInt64(), 456);
    EXPECT_EQ(json["path"].asString(), "/documents/file.txt");
    EXPECT_FALSE(json.isMember("error"));
    EXPECT_FALSE(json.isMember("data"));
}

TEST(BatchResultItem, RestoreFolderSuccessFlatFields) {
    BatchResultItem item;
    item.trash_id = 124;
    item.status = "success";
    item.folder_id = 789;
    item.path = "/documents/folder";

    auto json = item.ToJson();

    EXPECT_EQ(json["trash_id"].asUInt64(), 124);
    EXPECT_EQ(json["status"].asString(), "success");
    EXPECT_EQ(json["folder_id"].asUInt64(), 789);
    EXPECT_EQ(json["path"].asString(), "/documents/folder");
    EXPECT_FALSE(json.isMember("error"));
    EXPECT_FALSE(json.isMember("data"));
}

TEST(BatchResultItem, FailedItemWithErrorDetails) {
    BatchResultItem item;
    item.trash_id = 999;
    item.status = "failed";
    item.code = 10003;
    item.message = "Resource not found";
    item.field = "trash_id";
    item.value = "999";

    auto json = item.ToJson();

    EXPECT_EQ(json["trash_id"].asUInt64(), 999);
    EXPECT_EQ(json["status"].asString(), "failed");
    EXPECT_TRUE(json.isMember("error"));
    EXPECT_EQ(json["error"]["code"].asUInt(), 10003);
    EXPECT_EQ(json["error"]["message"].asString(), "Resource not found");
    EXPECT_EQ(json["error"]["field"].asString(), "trash_id");
    EXPECT_EQ(json["error"]["value"].asString(), "999");
    EXPECT_FALSE(json.isMember("data"));
}

TEST(BatchResultItem, DeleteSuccessFlatFreedSpace) {
    BatchResultItem item;
    item.trash_id = 100;
    item.status = "success";
    item.freed_space = 1048576;

    auto json = item.ToJson();

    EXPECT_EQ(json["trash_id"].asUInt64(), 100);
    EXPECT_EQ(json["status"].asString(), "success");
    EXPECT_EQ(json["freed_space"].asUInt64(), 1048576);
    EXPECT_FALSE(json.isMember("data"));
    EXPECT_FALSE(json.isMember("error"));
}

TEST(TrashItemResponse, ToJsonCorrectFields) {
    TrashItemResponse response;
    response.id = 1;
    response.type = "file";
    response.original_id = 100;
    response.name = "test_document.pdf";
    response.size = 2048576;
    response.original_path = "/documents/test_document.pdf";
    response.deleted_at = "2026-02-15T10:30:00Z";
    response.expires_at = "2026-03-17T10:30:00Z";

    auto json = response.ToJson();

    EXPECT_EQ(json["id"].asUInt64(), 1);
    EXPECT_EQ(json["type"].asString(), "file");
    EXPECT_EQ(json["original_id"].asUInt64(), 100);
    EXPECT_EQ(json["name"].asString(), "test_document.pdf");
    EXPECT_EQ(json["size"].asUInt64(), 2048576);
    EXPECT_EQ(json["original_path"].asString(), "/documents/test_document.pdf");
    EXPECT_EQ(json["deleted_at"].asString(), "2026-02-15T10:30:00Z");
    EXPECT_EQ(json["expires_at"].asString(), "2026-03-17T10:30:00Z");
    EXPECT_FALSE(json.isMember("original_folder_id"));
}

TEST(BatchRestoreResponse, ToJsonCorrectStructure) {
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
    success2.path = "/folder1";

    BatchResultItem failed1;
    failed1.trash_id = 3;
    failed1.status = "failed";
    failed1.code = 10003;
    failed1.message = "Resource not found";
    failed1.field = "trash_id";
    failed1.value = "3";

    response.results = { success1, success2, failed1 };

    auto json = response.ToJson();

    EXPECT_EQ(json["summary"]["total"].asInt(), 3);
    EXPECT_EQ(json["summary"]["success_count"].asInt(), 2);
    EXPECT_EQ(json["summary"]["failure_count"].asInt(), 1);
    EXPECT_EQ(json["results"].size(), 3u);

    EXPECT_EQ(json["results"][0]["trash_id"].asUInt64(), 1);
    EXPECT_EQ(json["results"][0]["status"].asString(), "success");
    EXPECT_EQ(json["results"][0]["file_id"].asUInt64(), 100);
    EXPECT_EQ(json["results"][0]["path"].asString(), "/file1.txt");

    EXPECT_EQ(json["results"][1]["trash_id"].asUInt64(), 2);
    EXPECT_EQ(json["results"][1]["status"].asString(), "success");
    EXPECT_EQ(json["results"][1]["folder_id"].asUInt64(), 200);
    EXPECT_EQ(json["results"][1]["path"].asString(), "/folder1");

    EXPECT_EQ(json["results"][2]["trash_id"].asUInt64(), 3);
    EXPECT_EQ(json["results"][2]["status"].asString(), "failed");
    EXPECT_EQ(json["results"][2]["error"]["code"].asUInt(), 10003);
    EXPECT_EQ(json["results"][2]["error"]["message"].asString(), "Resource not found");
    EXPECT_EQ(json["results"][2]["error"]["field"].asString(), "trash_id");
    EXPECT_EQ(json["results"][2]["error"]["value"].asString(), "3");
}

TEST(BatchDeleteResponse, ToJsonCorrectStructure) {
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

    response.results = { item1, item2 };

    auto json = response.ToJson();

    EXPECT_EQ(json["summary"]["total"].asInt(), 2);
    EXPECT_EQ(json["summary"]["success_count"].asInt(), 2);
    EXPECT_EQ(json["summary"]["failure_count"].asInt(), 0);
    EXPECT_EQ(json["results"].size(), 2u);

    EXPECT_EQ(json["results"][0]["trash_id"].asUInt64(), 10);
    EXPECT_EQ(json["results"][0]["status"].asString(), "success");
    EXPECT_EQ(json["results"][0]["freed_space"].asUInt64(), 1024);
    EXPECT_FALSE(json["results"][0].isMember("data"));

    EXPECT_EQ(json["results"][1]["trash_id"].asUInt64(), 11);
    EXPECT_EQ(json["results"][1]["status"].asString(), "success");
    EXPECT_EQ(json["results"][1]["freed_space"].asUInt64(), 2048);
    EXPECT_FALSE(json["results"][1].isMember("data"));
}

TEST(DeleteAllResponse, ToJsonCorrectFields) {
    DeleteAllResponse response;
    response.deleted_count = 15;
    response.freed_space = 15728640;

    auto json = response.ToJson();

    EXPECT_EQ(json["deleted_count"].asInt(), 15);
    EXPECT_EQ(json["freed_space"].asUInt64(), 15728640);
}

TEST(DeleteAllResponse, EmptyTrash) {
    DeleteAllResponse response;
    response.deleted_count = 0;
    response.freed_space = 0;

    auto json = response.ToJson();

    EXPECT_EQ(json["deleted_count"].asInt(), 0);
    EXPECT_EQ(json["freed_space"].asUInt64(), 0);
}
