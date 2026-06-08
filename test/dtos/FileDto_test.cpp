/**
 * @file FileDto_test.cpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief File DTO unit tests
 *
 * @copyright Copyright (c) 2026
 *
 */

#include "dtos/FileDto.hpp"

#include <string>

#include <drogon/HttpRequest.h>
#include <drogon/utils/Utilities.h>
#include <gtest/gtest.h>
#include <json/json.h>

#include "utils/ErrorCode.hpp"

using disk::Pagination;
using disk::file::CompleteUploadRequest;
using disk::file::CompleteUploadResponse;
using disk::file::CopyRequest;
using disk::file::DeleteRequest;
using disk::file::DownloadInfoRequest;
using disk::file::FileItem;
using disk::file::FileListItem;
using disk::file::FileListRequest;
using disk::file::FileListResponse;
using disk::file::InitUploadRequest;
using disk::file::InitUploadResponse;
using disk::file::MoveRequest;
using disk::file::MoveResponse;
using disk::file::RenameRequest;
using disk::file::SearchRequest;
using disk::file::SearchResponse;
using disk::file::SearchResultItem;

using disk::file::UploadChunkResponse;

/// ==================== Helper Functions ====================

static auto CreateJsonRequest(const Json::Value& json) -> drogon::HttpRequestPtr {
    Json::StreamWriterBuilder builder;
    builder["indentation"] = "";
    std::string body = Json::writeString(builder, json);

    auto req = drogon::HttpRequest::newHttpRequest();
    req->setBody(body);
    req->setContentTypeCode(drogon::CT_APPLICATION_JSON);

    return req;
}

static auto CreateInitUploadRequest(
    const std::string& filename,
    uint64_t file_size,
    const std::string& file_hash,
    uint64_t parent_id = 0
) -> drogon::HttpRequestPtr {
    Json::Value json;
    json["filename"] = filename;
    json["file_size"] = static_cast<Json::UInt64>(file_size);
    json["file_hash"] = file_hash;
    json["parent_id"] = static_cast<Json::UInt64>(parent_id);

    return CreateJsonRequest(json);
}

static auto CreateCompleteUploadRequest(const std::string& upload_id) -> drogon::HttpRequestPtr {
    Json::Value json;
    json["upload_id"] = upload_id;

    return CreateJsonRequest(json);
}

/// ==================== FileItem Tests ====================

TEST(FileItem, ToJsonCorrectFields) {
    FileItem item;
    item.id = 123;
    item.name = "document.pdf";
    item.size = 1048576;
    item.hash = "d41d8cd98f00b204e9800998ecf8427e";
    item.mime_type = "application/pdf";
    item.parent_id = 10;
    item.created_at = "2026-02-14T10:30:00Z";

    auto json = item.ToJson();

    EXPECT_EQ(json["id"].asUInt64(), 123);
    EXPECT_EQ(json["name"].asString(), "document.pdf");
    EXPECT_EQ(json["size"].asUInt64(), 1048576);
    EXPECT_EQ(json["hash"].asString(), "d41d8cd98f00b204e9800998ecf8427e");
    EXPECT_EQ(json["mime_type"].asString(), "application/pdf");
    EXPECT_EQ(json["parent_id"].asUInt64(), 10);
    EXPECT_EQ(json["created_at"].asString(), "2026-02-14T10:30:00Z");
}

TEST(FileItem, ToJsonMinimalFields) {
    FileItem item;
    item.id = 1;
    item.name = "test.txt";
    item.size = 0;
    item.hash = "";
    item.mime_type = "text/plain";
    item.parent_id = 0;
    item.created_at = "";

    auto json = item.ToJson();

    EXPECT_EQ(json["id"].asUInt64(), 1);
    EXPECT_EQ(json["name"].asString(), "test.txt");
}

/// ==================== InitUploadRequest Tests ====================

TEST(InitUploadRequest, ValidParameters) {
    auto req = CreateInitUploadRequest(
        "document.pdf",
        104857600,
        "d41d8cd98f00b204e9800998ecf8427e",
        0
    );
    auto result = InitUploadRequest::FromRequest(req);

    ASSERT_TRUE(result.has_value()) << "Valid parameters should pass";
    EXPECT_EQ(result->filename, "document.pdf");
    EXPECT_EQ(result->file_size, 104857600);
    EXPECT_EQ(result->file_hash, "d41d8cd98f00b204e9800998ecf8427e");
    EXPECT_EQ(result->parent_id, 0);
}

TEST(InitUploadRequest, ValidWithParentId) {
    auto req = CreateInitUploadRequest(
        "report.docx",
        5242880,
        "abc123def456789012345678901234ab",
        42
    );
    auto result = InitUploadRequest::FromRequest(req);

    ASSERT_TRUE(result.has_value()) << "Valid parameters with parent_id should pass";
    EXPECT_EQ(result->filename, "report.docx");
    EXPECT_EQ(result->parent_id, 42);
}

TEST(InitUploadRequest, MissingFilename) {
    Json::Value json;
    json["file_size"] = 1048576;
    json["file_hash"] = "d41d8cd98f00b204e9800998ecf8427e";
    json["parent_id"] = 0;

    auto req = CreateJsonRequest(json);
    auto result = InitUploadRequest::FromRequest(req);

    EXPECT_FALSE(result.has_value()) << "Missing filename should fail";
    if (!result.has_value()) {
        EXPECT_EQ(result.error().code, ErrorCode::ValidationFailed);
    }
}

TEST(InitUploadRequest, MissingFileHash) {
    Json::Value json;
    json["filename"] = "test.txt";
    json["file_size"] = 1024;
    json["parent_id"] = 0;

    auto req = CreateJsonRequest(json);
    auto result = InitUploadRequest::FromRequest(req);

    EXPECT_FALSE(result.has_value()) << "Missing file_hash should fail";
    if (!result.has_value()) {
        EXPECT_EQ(result.error().code, ErrorCode::ValidationFailed);
    }
}

TEST(InitUploadRequest, InvalidFileHashLength) {
    auto req = CreateInitUploadRequest(
        "test.txt",
        1024,
        "abc123", ///< 过短
        0
    );
    auto result = InitUploadRequest::FromRequest(req);

    EXPECT_FALSE(result.has_value()) << "Invalid hash length should fail";
    if (!result.has_value()) {
        EXPECT_EQ(result.error().code, ErrorCode::ValidationFailed);
    }
}

TEST(InitUploadRequest, InvalidFileHashNonHex) {
    auto req = CreateInitUploadRequest(
        "test.txt",
        1024,
        "ghijklmnopqrstuvwxijklmnopqrstuv", ///< 非十六进制字符
        0
    );
    auto result = InitUploadRequest::FromRequest(req);

    EXPECT_FALSE(result.has_value()) << "Non-hex hash should fail";
    if (!result.has_value()) {
        EXPECT_EQ(result.error().code, ErrorCode::ValidationFailed);
    }
}

TEST(InitUploadRequest, InvalidFileHashUppercase) {
    auto req = CreateInitUploadRequest(
        "test.txt",
        1024,
        "ABCDEF1234567890ABCDEF1234567890", ///< 大写（应失败）
        0
    );
    auto result = InitUploadRequest::FromRequest(req);

    EXPECT_FALSE(result.has_value()) << "Uppercase hash should fail";
    if (!result.has_value()) {
        EXPECT_EQ(result.error().code, ErrorCode::ValidationFailed);
    }
}

TEST(InitUploadRequest, FilenameTooLong) {
    std::string filename(256, 'a'); ///< 256 chars
    auto req = CreateInitUploadRequest(
        filename,
        1024,
        "d41d8cd98f00b204e9800998ecf8427e",
        0
    );
    auto result = InitUploadRequest::FromRequest(req);

    EXPECT_FALSE(result.has_value()) << "Filename > 255 chars should fail";
    if (!result.has_value()) {
        EXPECT_EQ(result.error().code, ErrorCode::ValidationFailed);
    }
}

TEST(InitUploadRequest, FilenameMaxLength) {
    std::string filename(255, 'a'); ///< 255 chars (max valid)
    auto req = CreateInitUploadRequest(
        filename,
        1024,
        "d41d8cd98f00b204e9800998ecf8427e",
        0
    );
    auto result = InitUploadRequest::FromRequest(req);

    ASSERT_TRUE(result.has_value()) << "Filename with 255 chars should pass";
    EXPECT_EQ(result->filename.length(), 255);
}

TEST(InitUploadRequest, FilenameWithChineseCharacters) {
    auto req = CreateInitUploadRequest(
        "B22041423_吴俊_剧本杀服务平台的设计与实现_毕业论文_0516.doc",
        1024,
        "d41d8cd98f00b204e9800998ecf8427e",
        0
    );
    auto result = InitUploadRequest::FromRequest(req);

    ASSERT_TRUE(result.has_value()) << "Valid UTF-8 Chinese filename should pass";
    EXPECT_EQ(result->filename, "B22041423_吴俊_剧本杀服务平台的设计与实现_毕业论文_0516.doc");
}

TEST(InitUploadRequest, FilenameEmpty) {
    auto req = CreateInitUploadRequest(
        "",
        1024,
        "d41d8cd98f00b204e9800998ecf8427e",
        0
    );
    auto result = InitUploadRequest::FromRequest(req);

    EXPECT_FALSE(result.has_value()) << "Empty filename should fail";
    if (!result.has_value()) {
        EXPECT_EQ(result.error().code, ErrorCode::ValidationFailed);
    }
}

TEST(InitUploadRequest, FilenameForbiddenCharSlash) {
    auto req = CreateInitUploadRequest(
        "test/file.txt",
        1024,
        "d41d8cd98f00b204e9800998ecf8427e",
        0
    );
    auto result = InitUploadRequest::FromRequest(req);

    EXPECT_FALSE(result.has_value()) << "Filename with / should fail";
    if (!result.has_value()) {
        EXPECT_EQ(result.error().code, ErrorCode::InvalidFilename);
    }
}

TEST(InitUploadRequest, FilenameForbiddenCharBackslash) {
    auto req = CreateInitUploadRequest(
        "test\\file.txt",
        1024,
        "d41d8cd98f00b204e9800998ecf8427e",
        0
    );
    auto result = InitUploadRequest::FromRequest(req);

    EXPECT_FALSE(result.has_value()) << "Filename with \\ should fail";
    if (!result.has_value()) {
        EXPECT_EQ(result.error().code, ErrorCode::InvalidFilename);
    }
}

TEST(InitUploadRequest, FilenameForbiddenCharColon) {
    auto req = CreateInitUploadRequest(
        "test:file.txt",
        1024,
        "d41d8cd98f00b204e9800998ecf8427e",
        0
    );
    auto result = InitUploadRequest::FromRequest(req);

    EXPECT_FALSE(result.has_value()) << "Filename with : should fail";
    if (!result.has_value()) {
        EXPECT_EQ(result.error().code, ErrorCode::InvalidFilename);
    }
}

TEST(InitUploadRequest, FileSizeZero) {
    auto req = CreateInitUploadRequest(
        "test.txt",
        0, ///< 零大小
        "d41d8cd98f00b204e9800998ecf8427e",
        0
    );
    auto result = InitUploadRequest::FromRequest(req);

    EXPECT_FALSE(result.has_value()) << "Zero file_size should fail";
    if (!result.has_value()) {
        EXPECT_EQ(result.error().code, ErrorCode::ValidationFailed);
    }
}

TEST(InitUploadRequest, InvalidJSON) {
    auto req = drogon::HttpRequest::newHttpRequest();
    req->setBody("{invalid json}");
    req->setContentTypeCode(drogon::CT_APPLICATION_JSON);

    auto result = InitUploadRequest::FromRequest(req);

    EXPECT_FALSE(result.has_value()) << "Invalid JSON should fail";
}

TEST(InitUploadRequest, FilenameWrongType) {
    Json::Value json;
    json["filename"] = 123;
    json["file_size"] = 1024;
    json["file_hash"] = "d41d8cd98f00b204e9800998ecf8427e";
    json["parent_id"] = 0;

    auto req = CreateJsonRequest(json);
    auto result = InitUploadRequest::FromRequest(req);

    EXPECT_FALSE(result.has_value()) << "Filename with wrong type should fail";
}

/// ==================== InitUploadResponse Tests ====================

TEST(InitUploadResponse, ToJsonNormalUpload) {
    InitUploadResponse response;
    response.upload_id = "up_abc123def456";
    response.chunk_size = 5242880;
    response.total_chunks = 20;
    response.uploaded_chunks = { 0, 1, 2 };
    response.instant_upload = false;

    auto json = response.ToJson();

    EXPECT_EQ(json["upload_id"].asString(), "up_abc123def456");
    EXPECT_EQ(json["chunk_size"].asUInt(), 5242880);
    EXPECT_EQ(json["total_chunks"].asUInt(), 20);
    EXPECT_TRUE(json["uploaded_chunks"].isArray());
    EXPECT_EQ(json["uploaded_chunks"].size(), 3);
    EXPECT_FALSE(json["instant_upload"].asBool());
    EXPECT_FALSE(json.isMember("file"));
}

TEST(InitUploadResponse, ToJsonInstantUpload) {
    InitUploadResponse response;
    response.upload_id = "";
    response.chunk_size = 0;
    response.total_chunks = 0;
    response.uploaded_chunks = {};
    response.instant_upload = true;

    FileItem file;
    file.id = 123;
    file.name = "existing.pdf";
    file.size = 1048576;
    file.hash = "d41d8cd98f00b204e9800998ecf8427e";
    file.mime_type = "application/pdf";
    file.parent_id = 0;
    file.created_at = "2026-02-14T10:30:00Z";
    response.file = file;

    auto json = response.ToJson();

    EXPECT_TRUE(json["instant_upload"].asBool());
    EXPECT_TRUE(json.isMember("file"));
    EXPECT_EQ(json["file"]["id"].asUInt64(), 123);
    EXPECT_EQ(json["file"]["name"].asString(), "existing.pdf");
}

TEST(InitUploadResponse, ToJsonResumeUpload) {
    InitUploadResponse response;
    response.upload_id = "up_resume_123";
    response.chunk_size = 5242880;
    response.total_chunks = 10;
    response.uploaded_chunks = { 0, 1, 2, 3, 4 }; ///< 前5个分片已上传
    response.instant_upload = false;

    auto json = response.ToJson();

    EXPECT_EQ(json["uploaded_chunks"].size(), 5);
    EXPECT_EQ(json["uploaded_chunks"][0].asUInt(), 0);
    EXPECT_EQ(json["uploaded_chunks"][4].asUInt(), 4);
}

/// ==================== UploadChunkResponse Tests ====================

TEST(UploadChunkResponse, ToJsonSuccess) {
    UploadChunkResponse response;
    response.chunk_index = 5;
    response.uploaded = true;

    auto json = response.ToJson();

    EXPECT_EQ(json["chunk_index"].asUInt(), 5);
    EXPECT_TRUE(json["uploaded"].asBool());
}

TEST(UploadChunkResponse, ToJsonFailure) {
    UploadChunkResponse response;
    response.chunk_index = 10;
    response.uploaded = false;

    auto json = response.ToJson();

    EXPECT_EQ(json["chunk_index"].asUInt(), 10);
    EXPECT_FALSE(json["uploaded"].asBool());
}

/// ==================== CompleteUploadRequest Tests ====================

TEST(CompleteUploadRequest, ValidParameters) {
    auto req = CreateCompleteUploadRequest("up_abc123def456");
    auto result = CompleteUploadRequest::FromRequest(req);

    ASSERT_TRUE(result.has_value()) << "Valid parameters should pass";
    EXPECT_EQ(result->upload_id, "up_abc123def456");
}

TEST(CompleteUploadRequest, MissingUploadId) {
    Json::Value json;

    auto req = CreateJsonRequest(json);
    auto result = CompleteUploadRequest::FromRequest(req);

    EXPECT_FALSE(result.has_value()) << "Missing upload_id should fail";
    if (!result.has_value()) {
        EXPECT_EQ(result.error().code, ErrorCode::ValidationFailed);
    }
}

TEST(CompleteUploadRequest, EmptyUploadId) {
    auto req = CreateCompleteUploadRequest("");
    auto result = CompleteUploadRequest::FromRequest(req);

    EXPECT_FALSE(result.has_value()) << "Empty upload_id should fail";
    if (!result.has_value()) {
        EXPECT_EQ(result.error().code, ErrorCode::ValidationFailed);
    }
}

TEST(CompleteUploadRequest, InvalidJSON) {
    auto req = drogon::HttpRequest::newHttpRequest();
    req->setBody("{invalid json}");
    req->setContentTypeCode(drogon::CT_APPLICATION_JSON);

    auto result = CompleteUploadRequest::FromRequest(req);

    EXPECT_FALSE(result.has_value()) << "Invalid JSON should fail";
}

/// ==================== CompleteUploadResponse Tests ====================

TEST(CompleteUploadResponse, ToJsonCorrectFields) {
    CompleteUploadResponse response;
    response.file.id = 456;
    response.file.name = "completed.pdf";
    response.file.size = 10485760;
    response.file.hash = "d41d8cd98f00b204e9800998ecf8427e";
    response.file.mime_type = "application/pdf";
    response.file.parent_id = 10;
    response.file.created_at = "2026-02-14T11:00:00Z";

    auto json = response.ToJson();

    EXPECT_TRUE(json.isMember("file"));
    EXPECT_EQ(json["file"]["id"].asUInt64(), 456);
    EXPECT_EQ(json["file"]["name"].asString(), "completed.pdf");
    EXPECT_EQ(json["file"]["size"].asUInt64(), 10485760);
    EXPECT_EQ(json["file"]["hash"].asString(), "d41d8cd98f00b204e9800998ecf8427e");
    EXPECT_EQ(json["file"]["mime_type"].asString(), "application/pdf");
    EXPECT_EQ(json["file"]["parent_id"].asUInt64(), 10);
    EXPECT_EQ(json["file"]["created_at"].asString(), "2026-02-14T11:00:00Z");
}

/// ==================== FileListRequest Tests ====================

TEST(FileListRequest, ValidParameters) {
    auto req = drogon::HttpRequest::newHttpRequest();
    req->setParameter("parent_id", "10");
    req->setParameter("page", "2");
    req->setParameter("page_size", "50");
    req->setParameter("sort_by", "size");
    req->setParameter("sort_order", "desc");
    req->setParameter("type", "file");

    auto result = FileListRequest::FromRequest(req);
    ASSERT_TRUE(result.has_value()) << "Valid parameters should pass";
    EXPECT_EQ(result->parent_id, 10);
    EXPECT_EQ(result->page, 2);
    EXPECT_EQ(result->page_size, 50);
    EXPECT_EQ(result->sort_by, "size");
    EXPECT_EQ(result->sort_order, "desc");
    EXPECT_EQ(result->type, "file");
}

TEST(FileListRequest, DefaultValues) {
    auto req = drogon::HttpRequest::newHttpRequest();

    auto result = FileListRequest::FromRequest(req);
    ASSERT_TRUE(result.has_value()) << "Empty request should use defaults";
    EXPECT_EQ(result->parent_id, 0);
    EXPECT_EQ(result->page, 1);
    EXPECT_EQ(result->page_size, 20);
    EXPECT_EQ(result->sort_by, "name");
    EXPECT_EQ(result->sort_order, "asc");
    EXPECT_EQ(result->type, "all");
}

TEST(FileListRequest, InvalidPage) {
    auto req = drogon::HttpRequest::newHttpRequest();
    req->setParameter("page", "-1");

    auto result = FileListRequest::FromRequest(req);
    EXPECT_FALSE(result.has_value()) << "Negative page should fail";
    if (!result.has_value()) {
        EXPECT_EQ(result.error().code, ErrorCode::ValidationFailed);
    }
}

TEST(FileListRequest, InvalidPageSizeTooLarge) {
    auto req = drogon::HttpRequest::newHttpRequest();
    req->setParameter("page_size", "101");

    auto result = FileListRequest::FromRequest(req);
    EXPECT_FALSE(result.has_value()) << "page_size > 100 should fail";
    if (!result.has_value()) {
        EXPECT_EQ(result.error().code, ErrorCode::ValidationFailed);
    }
}

TEST(FileListRequest, InvalidPageSizeZero) {
    auto req = drogon::HttpRequest::newHttpRequest();
    req->setParameter("page_size", "0");

    auto result = FileListRequest::FromRequest(req);
    EXPECT_FALSE(result.has_value()) << "page_size = 0 should fail";
    if (!result.has_value()) {
        EXPECT_EQ(result.error().code, ErrorCode::ValidationFailed);
    }
}

TEST(FileListRequest, InvalidSortBy) {
    auto req = drogon::HttpRequest::newHttpRequest();
    req->setParameter("sort_by", "invalid_field");

    auto result = FileListRequest::FromRequest(req);
    EXPECT_FALSE(result.has_value()) << "Invalid sort_by should fail";
    if (!result.has_value()) {
        EXPECT_EQ(result.error().code, ErrorCode::ValidationFailed);
    }
}

TEST(FileListRequest, InvalidType) {
    auto req = drogon::HttpRequest::newHttpRequest();
    req->setParameter("type", "document");

    auto result = FileListRequest::FromRequest(req);
    EXPECT_FALSE(result.has_value()) << "Invalid type should fail";
    if (!result.has_value()) {
        EXPECT_EQ(result.error().code, ErrorCode::ValidationFailed);
    }
}

/// ==================== DownloadInfoRequest Tests ====================

TEST(DownloadInfoRequest, ValidFileId) {
    auto result = DownloadInfoRequest::FromPath("123");
    ASSERT_TRUE(result.has_value()) << "Valid file_id should pass";
    EXPECT_EQ(result->file_id, 123);
}

TEST(DownloadInfoRequest, InvalidFileIdNonNumeric) {
    auto result = DownloadInfoRequest::FromPath("abc");
    EXPECT_FALSE(result.has_value()) << "Non-numeric file_id should fail";
    if (!result.has_value()) {
        EXPECT_EQ(result.error().code, ErrorCode::InvalidParameter);
    }
}

TEST(DownloadInfoRequest, InvalidFileIdZero) {
    auto result = DownloadInfoRequest::FromPath("0");
    EXPECT_FALSE(result.has_value()) << "file_id = 0 should fail";
    if (!result.has_value()) {
        EXPECT_EQ(result.error().code, ErrorCode::InvalidParameter);
    }
}

TEST(DownloadInfoRequest, InvalidFileIdNegative) {
    auto result = DownloadInfoRequest::FromPath("-5");
    EXPECT_FALSE(result.has_value()) << "Negative file_id should fail";
    if (!result.has_value()) {
        EXPECT_EQ(result.error().code, ErrorCode::InvalidParameter);
    }
}

TEST(DownloadInfoRequest, InvalidFileIdEmpty) {
    auto result = DownloadInfoRequest::FromPath("");
    EXPECT_FALSE(result.has_value()) << "Empty file_id should fail";
    if (!result.has_value()) {
        EXPECT_EQ(result.error().code, ErrorCode::InvalidParameter);
    }
}

/// ==================== RenameRequest Tests ====================

static auto CreateRenameRequest(uint64_t file_id, const std::string& new_name) -> drogon::HttpRequestPtr {
    Json::Value json;
    json["new_name"] = new_name;

    return CreateJsonRequest(json);
}

TEST(RenameRequest, ValidRequest) {
    auto req = CreateRenameRequest(123, "new_document.pdf");
    auto result = RenameRequest::FromPathAndRequest("123", req);

    ASSERT_TRUE(result.has_value()) << "Valid rename request should pass";
    EXPECT_EQ(result->file_id, 123);
    EXPECT_EQ(result->new_name, "new_document.pdf");
}

TEST(RenameRequest, ValidUtf8Name) {
    auto req = CreateRenameRequest(123, "毕业论文_最终版.doc");
    auto result = RenameRequest::FromPathAndRequest("123", req);

    ASSERT_TRUE(result.has_value()) << "Valid UTF-8 rename target should pass";
    EXPECT_EQ(result->file_id, 123);
    EXPECT_EQ(result->new_name, "毕业论文_最终版.doc");
}

TEST(RenameRequest, EmptyName) {
    auto req = CreateRenameRequest(123, "");
    auto result = RenameRequest::FromPathAndRequest("123", req);

    EXPECT_FALSE(result.has_value()) << "Empty new_name should fail";
    if (!result.has_value()) {
        EXPECT_EQ(result.error().code, ErrorCode::ValidationFailed);
    }
}

TEST(RenameRequest, InvalidFilenameSlash) {
    auto req = CreateRenameRequest(123, "test/file.txt");
    auto result = RenameRequest::FromPathAndRequest("123", req);

    EXPECT_FALSE(result.has_value()) << "Filename with / should fail";
    if (!result.has_value()) {
        EXPECT_EQ(result.error().code, ErrorCode::InvalidFilename);
    }
}

TEST(RenameRequest, InvalidFilenameBackslash) {
    auto req = CreateRenameRequest(123, "test\\file.txt");
    auto result = RenameRequest::FromPathAndRequest("123", req);

    EXPECT_FALSE(result.has_value()) << "Filename with \\ should fail";
    if (!result.has_value()) {
        EXPECT_EQ(result.error().code, ErrorCode::InvalidFilename);
    }
}

TEST(RenameRequest, InvalidFilenameColon) {
    auto req = CreateRenameRequest(123, "test:file.txt");
    auto result = RenameRequest::FromPathAndRequest("123", req);

    EXPECT_FALSE(result.has_value()) << "Filename with : should fail";
    if (!result.has_value()) {
        EXPECT_EQ(result.error().code, ErrorCode::InvalidFilename);
    }
}

TEST(RenameRequest, InvalidFilenameHiddenFile) {
    auto req = CreateRenameRequest(123, ".hidden");
    auto result = RenameRequest::FromPathAndRequest("123", req);

    EXPECT_FALSE(result.has_value()) << "Hidden filename should fail";
    if (!result.has_value()) {
        EXPECT_EQ(result.error().code, ErrorCode::InvalidFilename);
    }
}

TEST(RenameRequest, InvalidFilenameReservedDot) {
    auto req = CreateRenameRequest(123, ".");
    auto result = RenameRequest::FromPathAndRequest("123", req);

    EXPECT_FALSE(result.has_value()) << "Reserved name '.' should fail";
    if (!result.has_value()) {
        EXPECT_EQ(result.error().code, ErrorCode::InvalidFilename);
    }
}

TEST(RenameRequest, InvalidFilenameReservedDoubleDot) {
    auto req = CreateRenameRequest(123, "..");
    auto result = RenameRequest::FromPathAndRequest("123", req);

    EXPECT_FALSE(result.has_value()) << "Reserved name '..' should fail";
    if (!result.has_value()) {
        EXPECT_EQ(result.error().code, ErrorCode::InvalidFilename);
    }
}

TEST(RenameRequest, InvalidFileIdNonNumeric) {
    auto req = CreateRenameRequest(123, "valid.txt");
    auto result = RenameRequest::FromPathAndRequest("abc", req);

    EXPECT_FALSE(result.has_value()) << "Non-numeric file_id should fail";
    if (!result.has_value()) {
        EXPECT_EQ(result.error().code, ErrorCode::InvalidParameter);
    }
}

TEST(RenameRequest, MissingNewName) {
    Json::Value json;
    auto req = CreateJsonRequest(json);
    auto result = RenameRequest::FromPathAndRequest("123", req);

    EXPECT_FALSE(result.has_value()) << "Missing new_name should fail";
    if (!result.has_value()) {
        EXPECT_EQ(result.error().code, ErrorCode::ValidationFailed);
    }
}

/// ==================== MoveRequest Tests ====================

static auto CreateMoveRequest(
    const std::vector<uint64_t>& file_ids,
    uint64_t target_folder_id = 0,
    const std::vector<uint64_t>& folder_ids = {}
) -> drogon::HttpRequestPtr {
    Json::Value json;
    Json::Value file_ids_array(Json::arrayValue);
    for (auto id : file_ids) {
        file_ids_array.append(static_cast<Json::UInt64>(id));
    }
    Json::Value folder_ids_array(Json::arrayValue);
    for (auto id : folder_ids) {
        folder_ids_array.append(static_cast<Json::UInt64>(id));
    }
    json["file_ids"] = file_ids_array;
    json["folder_ids"] = folder_ids_array;
    json["target_folder_id"] = static_cast<Json::UInt64>(target_folder_id);

    return CreateJsonRequest(json);
}

TEST(MoveRequest, ValidRequest) {
    auto req = CreateMoveRequest({ 1, 2, 3 }, 10);
    auto result = MoveRequest::FromRequest(req);

    ASSERT_TRUE(result.has_value()) << "Valid move request should pass";
    EXPECT_EQ(result->file_ids.size(), 3);
    EXPECT_EQ(result->file_ids[0], 1);
    EXPECT_EQ(result->file_ids[1], 2);
    EXPECT_EQ(result->file_ids[2], 3);
    EXPECT_EQ(result->target_folder_id, 10);
}

TEST(MoveRequest, EmptyFileIdsWithFoldersPasses) {
    auto req = CreateMoveRequest({}, 10, { 20, 21 });
    auto result = MoveRequest::FromRequest(req);

    ASSERT_TRUE(result.has_value()) << "Folder-only move request should pass";
    EXPECT_TRUE(result->file_ids.empty());
    EXPECT_EQ(result->folder_ids.size(), 2);
    EXPECT_EQ(result->folder_ids[0], 20);
    EXPECT_EQ(result->folder_ids[1], 21);
    EXPECT_EQ(result->target_folder_id, 10);
}

TEST(MoveRequest, BothArraysEmpty) {
    auto req = CreateMoveRequest({});
    auto result = MoveRequest::FromRequest(req);

    EXPECT_FALSE(result.has_value()) << "Move request without file or folder ids should fail";
    if (!result.has_value()) {
        EXPECT_EQ(result.error().code, ErrorCode::InvalidParameter);
    }
}

TEST(MoveRequest, MissingFileIdsAndFolderIds) {
    Json::Value json;
    auto req = CreateJsonRequest(json);
    auto result = MoveRequest::FromRequest(req);

    EXPECT_FALSE(result.has_value()) << "Missing both id arrays should fail";
    if (!result.has_value()) {
        EXPECT_EQ(result.error().code, ErrorCode::InvalidParameter);
    }
}

TEST(MoveRequest, InvalidFileIdType) {
    Json::Value json;
    Json::Value ids_array(Json::arrayValue);
    ids_array.append("not_a_number");
    json["file_ids"] = ids_array;

    auto req = CreateJsonRequest(json);
    auto result = MoveRequest::FromRequest(req);

    EXPECT_FALSE(result.has_value()) << "Non-numeric file_id should fail";
    if (!result.has_value()) {
        EXPECT_EQ(result.error().code, ErrorCode::InvalidParameter);
    }
}

TEST(MoveRequest, InvalidFileIdZero) {
    Json::Value json;
    Json::Value ids_array(Json::arrayValue);
    ids_array.append(0);
    json["file_ids"] = ids_array;

    auto req = CreateJsonRequest(json);
    auto result = MoveRequest::FromRequest(req);

    EXPECT_FALSE(result.has_value()) << "file_id = 0 should fail";
    if (!result.has_value()) {
        EXPECT_EQ(result.error().code, ErrorCode::InvalidParameter);
    }
}

TEST(MoveRequest, InvalidFolderIdZero) {
    auto req = CreateMoveRequest({}, 0, { 0 });
    auto result = MoveRequest::FromRequest(req);

    EXPECT_FALSE(result.has_value()) << "folder_id = 0 should fail";
    if (!result.has_value()) {
        EXPECT_EQ(result.error().code, ErrorCode::InvalidParameter);
    }
}

TEST(MoveResponse, ToJsonIncludesFileAndFolderCounts) {
    MoveResponse response;
    response.moved_count = 3;
    response.moved_file_count = 2;
    response.moved_folder_count = 1;

    auto json = response.ToJson();

    EXPECT_EQ(json["moved_count"].asInt(), 3);
    EXPECT_EQ(json["moved_file_count"].asInt(), 2);
    EXPECT_EQ(json["moved_folder_count"].asInt(), 1);
}

/// ==================== CopyRequest Tests ====================

static auto CreateCopyRequest(
    const std::vector<uint64_t>& file_ids,
    uint64_t target_folder_id = 0,
    const std::vector<uint64_t>& folder_ids = {}
) -> drogon::HttpRequestPtr {
    Json::Value json;
    Json::Value file_ids_array(Json::arrayValue);
    for (auto id : file_ids) {
        file_ids_array.append(static_cast<Json::UInt64>(id));
    }
    json["file_ids"] = file_ids_array;

    if (!folder_ids.empty()) {
        Json::Value folder_ids_array(Json::arrayValue);
        for (auto id : folder_ids) {
            folder_ids_array.append(static_cast<Json::UInt64>(id));
        }
        json["folder_ids"] = folder_ids_array;
    }

    json["target_folder_id"] = static_cast<Json::UInt64>(target_folder_id);

    return CreateJsonRequest(json);
}

TEST(CopyRequest, ValidRequest) {
    auto req = CreateCopyRequest({ 100, 200 }, 5);
    auto result = CopyRequest::FromRequest(req);

    ASSERT_TRUE(result.has_value()) << "Valid copy request should pass";
    EXPECT_EQ(result->file_ids.size(), 2);
    EXPECT_EQ(result->file_ids[0], 100);
    EXPECT_EQ(result->file_ids[1], 200);
    EXPECT_TRUE(result->folder_ids.empty());
    EXPECT_EQ(result->target_folder_id, 5);
}

TEST(CopyRequest, ValidFolderOnlyRequest) {
    auto req = CreateCopyRequest({}, 5, { 10, 11 });
    auto result = CopyRequest::FromRequest(req);

    ASSERT_TRUE(result.has_value()) << "Folder-only copy request should pass";
    EXPECT_TRUE(result->file_ids.empty());
    EXPECT_EQ(result->folder_ids.size(), 2);
    EXPECT_EQ(result->folder_ids[0], 10);
    EXPECT_EQ(result->folder_ids[1], 11);
    EXPECT_EQ(result->target_folder_id, 5);
}

TEST(CopyRequest, ValidMixedRequest) {
    auto req = CreateCopyRequest({ 100 }, 5, { 10 });
    auto result = CopyRequest::FromRequest(req);

    ASSERT_TRUE(result.has_value()) << "Mixed file/folder copy request should pass";
    EXPECT_EQ(result->file_ids.size(), 1);
    EXPECT_EQ(result->file_ids[0], 100);
    EXPECT_EQ(result->folder_ids.size(), 1);
    EXPECT_EQ(result->folder_ids[0], 10);
    EXPECT_EQ(result->target_folder_id, 5);
}

TEST(CopyRequest, EmptyFileAndFolderIds) {
    auto req = CreateCopyRequest({});
    auto result = CopyRequest::FromRequest(req);

    EXPECT_FALSE(result.has_value()) << "Empty file_ids and folder_ids should fail";
    if (!result.has_value()) {
        EXPECT_EQ(result.error().code, ErrorCode::InvalidParameter);
    }
}

TEST(CopyRequest, MissingFileAndFolderIds) {
    Json::Value json;
    auto req = CreateJsonRequest(json);
    auto result = CopyRequest::FromRequest(req);

    EXPECT_FALSE(result.has_value()) << "Missing file_ids and folder_ids should fail";
    if (!result.has_value()) {
        EXPECT_EQ(result.error().code, ErrorCode::InvalidParameter);
    }
}

TEST(CopyRequest, InvalidFileIdType) {
    Json::Value json;
    Json::Value ids_array(Json::arrayValue);
    ids_array.append(3.14);
    json["file_ids"] = ids_array;

    auto req = CreateJsonRequest(json);
    auto result = CopyRequest::FromRequest(req);

    EXPECT_FALSE(result.has_value()) << "Non-integer file_id should fail";
    if (!result.has_value()) {
        EXPECT_EQ(result.error().code, ErrorCode::InvalidParameter);
    }
}

TEST(CopyRequest, InvalidFolderIdsType) {
    Json::Value json;
    json["folder_ids"] = "not-array";

    auto req = CreateJsonRequest(json);
    auto result = CopyRequest::FromRequest(req);

    EXPECT_FALSE(result.has_value()) << "Non-array folder_ids should fail";
    if (!result.has_value()) {
        EXPECT_EQ(result.error().code, ErrorCode::InvalidParameter);
    }
}

TEST(CopyRequest, InvalidFolderIdItemType) {
    Json::Value json;
    Json::Value ids_array(Json::arrayValue);
    ids_array.append("abc");
    json["folder_ids"] = ids_array;

    auto req = CreateJsonRequest(json);
    auto result = CopyRequest::FromRequest(req);

    EXPECT_FALSE(result.has_value()) << "Non-integer folder_id should fail";
    if (!result.has_value()) {
        EXPECT_EQ(result.error().code, ErrorCode::InvalidParameter);
    }
}

/// ==================== DeleteRequest Tests ====================

static auto CreateDeleteRequest(
    const std::vector<uint64_t>& file_ids,
    const std::vector<uint64_t>& folder_ids = {}
) -> drogon::HttpRequestPtr {
    Json::Value json;
    Json::Value file_ids_array(Json::arrayValue);
    for (auto id : file_ids) {
        file_ids_array.append(static_cast<Json::UInt64>(id));
    }
    json["file_ids"] = file_ids_array;

    if (!folder_ids.empty()) {
        Json::Value folder_ids_array(Json::arrayValue);
        for (auto id : folder_ids) {
            folder_ids_array.append(static_cast<Json::UInt64>(id));
        }
        json["folder_ids"] = folder_ids_array;
    }

    return CreateJsonRequest(json);
}

TEST(DeleteRequest, ValidRequest) {
    auto req = CreateDeleteRequest({ 1, 2, 3 });
    auto result = DeleteRequest::FromRequest(req);

    ASSERT_TRUE(result.has_value()) << "Valid delete request should pass";
    EXPECT_EQ(result->file_ids.size(), 3);
    EXPECT_EQ(result->file_ids[0], 1);
    EXPECT_EQ(result->file_ids[1], 2);
    EXPECT_EQ(result->file_ids[2], 3);
}

TEST(DeleteRequest, EmptyFileIds) {
    auto req = CreateDeleteRequest({}, { 2 });
    auto result = DeleteRequest::FromRequest(req);

    ASSERT_TRUE(result.has_value()) << "Empty file_ids with folder_ids should pass";
    EXPECT_TRUE(result->file_ids.empty());
    EXPECT_EQ(result->folder_ids.size(), 1);
    EXPECT_EQ(result->folder_ids[0], 2);
}

TEST(DeleteRequest, FolderOnlyRequest) {
    Json::Value json;
    Json::Value folder_ids(Json::arrayValue);
    folder_ids.append(Json::UInt64(10));
    json["folder_ids"] = folder_ids;

    auto req = CreateJsonRequest(json);
    auto result = DeleteRequest::FromRequest(req);

    ASSERT_TRUE(result.has_value()) << "Folder-only delete request should pass";
    EXPECT_TRUE(result->file_ids.empty());
    EXPECT_EQ(result->folder_ids.size(), 1);
    EXPECT_EQ(result->folder_ids[0], 10);
}

TEST(DeleteRequest, MixedFileAndFolderRequest) {
    auto req = CreateDeleteRequest({ 1 }, { 2 });
    auto result = DeleteRequest::FromRequest(req);

    ASSERT_TRUE(result.has_value()) << "Mixed delete request should pass";
    EXPECT_EQ(result->file_ids.size(), 1);
    EXPECT_EQ(result->file_ids[0], 1);
    EXPECT_EQ(result->folder_ids.size(), 1);
    EXPECT_EQ(result->folder_ids[0], 2);
}

TEST(DeleteRequest, BothArraysEmpty) {
    Json::Value json;
    json["file_ids"] = Json::Value(Json::arrayValue);
    json["folder_ids"] = Json::Value(Json::arrayValue);
    auto req = CreateJsonRequest(json);
    auto result = DeleteRequest::FromRequest(req);

    EXPECT_FALSE(result.has_value()) << "Both empty arrays should fail";
    if (!result.has_value()) {
        EXPECT_EQ(result.error().code, ErrorCode::InvalidParameter);
    }
}

TEST(DeleteRequest, MissingFileIds) {
    Json::Value json;
    auto req = CreateJsonRequest(json);
    auto result = DeleteRequest::FromRequest(req);

    EXPECT_FALSE(result.has_value()) << "Missing file_ids and folder_ids should fail";
    if (!result.has_value()) {
        EXPECT_EQ(result.error().code, ErrorCode::InvalidParameter);
    }
}

TEST(DeleteRequest, InvalidFileIdZero) {
    auto req = CreateDeleteRequest({ 1, 0, 2 });
    auto result = DeleteRequest::FromRequest(req);

    EXPECT_FALSE(result.has_value()) << "file_id = 0 should fail";
    if (!result.has_value()) {
        EXPECT_EQ(result.error().code, ErrorCode::InvalidParameter);
    }
}

TEST(DeleteRequest, InvalidFolderIdZero) {
    auto req = CreateDeleteRequest({}, { 1, 0, 2 });
    auto result = DeleteRequest::FromRequest(req);

    EXPECT_FALSE(result.has_value()) << "folder_id = 0 should fail";
    if (!result.has_value()) {
        EXPECT_EQ(result.error().code, ErrorCode::InvalidParameter);
    }
}

TEST(DeleteResponse, ToJsonIncludesFileAndFolderCounts) {
    disk::file::DeleteResponse response;
    response.deleted_count = 3;
    response.deleted_file_count = 1;
    response.deleted_folder_count = 2;

    auto json = response.ToJson();

    EXPECT_EQ(json["deleted_count"].asInt(), 3);
    EXPECT_EQ(json["deleted_file_count"].asInt(), 1);
    EXPECT_EQ(json["deleted_folder_count"].asInt(), 2);
}

/// ==================== FileListItem Tests ====================

TEST(FileListItem, ToJsonFileType) {
    FileListItem item;
    item.id = 123;
    item.name = "document.pdf";
    item.type = "file";
    item.size = 1048576;
    item.mime_type = "application/pdf";
    item.hash = "d41d8cd98f00b204e9800998ecf8427e";
    item.created_at = "2026-02-14T10:30:00Z";
    item.updated_at = "2026-02-15T11:00:00Z";

    auto json = item.ToJson();

    EXPECT_EQ(json["id"].asUInt64(), 123);
    EXPECT_EQ(json["name"].asString(), "document.pdf");
    EXPECT_EQ(json["type"].asString(), "file");
    EXPECT_EQ(json["size"].asUInt64(), 1048576);
    EXPECT_EQ(json["mime_type"].asString(), "application/pdf");
    EXPECT_EQ(json["hash"].asString(), "d41d8cd98f00b204e9800998ecf8427e");
    EXPECT_EQ(json["created_at"].asString(), "2026-02-14T10:30:00Z");
    EXPECT_EQ(json["updated_at"].asString(), "2026-02-15T11:00:00Z");
    /// File type should NOT have item_count
    EXPECT_FALSE(json.isMember("item_count"));
}

TEST(FileListItem, ToJsonFolderType) {
    FileListItem item;
    item.id = 456;
    item.name = "Documents";
    item.type = "folder";
    item.item_count = 42;
    item.created_at = "2026-02-14T10:30:00Z";
    item.updated_at = "2026-02-15T11:00:00Z";

    auto json = item.ToJson();

    EXPECT_EQ(json["id"].asUInt64(), 456);
    EXPECT_EQ(json["name"].asString(), "Documents");
    EXPECT_EQ(json["type"].asString(), "folder");
    EXPECT_EQ(json["item_count"].asInt(), 42);
    EXPECT_EQ(json["created_at"].asString(), "2026-02-14T10:30:00Z");
    EXPECT_EQ(json["updated_at"].asString(), "2026-02-15T11:00:00Z");
    /// Folder type should NOT have size, mime_type, hash
    EXPECT_FALSE(json.isMember("size"));
    EXPECT_FALSE(json.isMember("mime_type"));
    EXPECT_FALSE(json.isMember("hash"));
}

/// ==================== FileListResponse Tests ====================

TEST(FileListResponse, ToJsonWithPaginationMetadata) {
    FileListResponse response;

    FileListItem item1;
    item1.id = 1;
    item1.name = "file1.txt";
    item1.type = "file";
    item1.size = 1024;
    item1.mime_type = "text/plain";
    item1.hash = "abc123def456789012345678901234ab";
    item1.created_at = "2026-02-14T10:30:00Z";
    item1.updated_at = "2026-02-14T10:30:00Z";

    FileListItem item2;
    item2.id = 2;
    item2.name = "folder1";
    item2.type = "folder";
    item2.item_count = 5;
    item2.created_at = "2026-02-13T10:30:00Z";
    item2.updated_at = "2026-02-14T11:00:00Z";

    response.items.push_back(item1);
    response.items.push_back(item2);

    response.pagination.page = 2;
    response.pagination.page_size = 20;
    response.pagination.total = 45;
    response.pagination.total_pages = 3;

    auto json = response.ToJson();

    /// Verify items array
    ASSERT_TRUE(json.isMember("items"));
    EXPECT_EQ(json["items"].size(), 2U);
    EXPECT_EQ(json["items"][0]["name"].asString(), "file1.txt");
    EXPECT_EQ(json["items"][1]["name"].asString(), "folder1");

    /// Verify pagination metadata structure
    ASSERT_TRUE(json.isMember("pagination"));
    EXPECT_EQ(json["pagination"]["page"].asInt(), 2);
    EXPECT_EQ(json["pagination"]["page_size"].asInt(), 20);
    EXPECT_EQ(json["pagination"]["total"].asInt(), 45);
    EXPECT_EQ(json["pagination"]["total_pages"].asInt(), 3);
}

TEST(FileListResponse, PaginationMetadataFieldNames) {
    /// Regression test: ensure pagination field names are stable
    FileListResponse response;
    response.pagination.page = 1;
    response.pagination.page_size = 10;
    response.pagination.total = 100;
    response.pagination.total_pages = 10;

    auto json = response.ToJson();
    auto pagination = json["pagination"];

    /// Verify exact field names
    EXPECT_TRUE(pagination.isMember("page"));
    EXPECT_TRUE(pagination.isMember("page_size"));
    EXPECT_TRUE(pagination.isMember("total"));
    EXPECT_TRUE(pagination.isMember("total_pages"));

    /// Verify field types
    EXPECT_TRUE(pagination["page"].isInt());
    EXPECT_TRUE(pagination["page_size"].isInt());
    EXPECT_TRUE(pagination["total"].isInt());
    EXPECT_TRUE(pagination["total_pages"].isInt());
}

TEST(FileListResponse, EmptyListWithPagination) {
    FileListResponse response;
    response.pagination.page = 1;
    response.pagination.page_size = 20;
    response.pagination.total = 0;
    response.pagination.total_pages = 0;

    auto json = response.ToJson();

    EXPECT_TRUE(json["items"].isArray());
    EXPECT_EQ(json["items"].size(), 0U);
    EXPECT_EQ(json["pagination"]["total"].asInt(), 0);
    EXPECT_EQ(json["pagination"]["total_pages"].asInt(), 0);
}

/// ==================== SearchRequest Tests ====================

TEST(SearchRequest, ValidParameters) {
    auto req = drogon::HttpRequest::newHttpRequest();
    req->setParameter("keyword", "document");
    req->setParameter("type", "file");
    req->setParameter("folder_id", "10");
    req->setParameter("page", "2");
    req->setParameter("page_size", "50");

    auto result = SearchRequest::FromRequest(req);
    ASSERT_TRUE(result.has_value()) << "Valid parameters should pass";
    EXPECT_EQ(result->keyword, "document");
    EXPECT_EQ(result->type, "file");
    ASSERT_TRUE(result->folder_id.has_value());
    EXPECT_EQ(*result->folder_id, 10);
    EXPECT_EQ(result->page, 2);
    EXPECT_EQ(result->page_size, 50);
}

TEST(SearchRequest, DefaultValues) {
    auto req = drogon::HttpRequest::newHttpRequest();
    req->setParameter("keyword", "test");

    auto result = SearchRequest::FromRequest(req);
    ASSERT_TRUE(result.has_value()) << "Valid keyword should pass";
    EXPECT_EQ(result->keyword, "test");
    EXPECT_EQ(result->type, "all");
    EXPECT_FALSE(result->folder_id.has_value());
    EXPECT_EQ(result->page, 1);
    EXPECT_EQ(result->page_size, 20);
}

TEST(SearchRequest, MissingKeyword) {
    auto req = drogon::HttpRequest::newHttpRequest();

    auto result = SearchRequest::FromRequest(req);
    EXPECT_FALSE(result.has_value()) << "Missing keyword should fail";
    if (!result.has_value()) {
        EXPECT_EQ(result.error().code, ErrorCode::ValidationFailed);
    }
}

TEST(SearchRequest, KeywordTooLong) {
    std::string long_keyword(101, 'a');
    auto req = drogon::HttpRequest::newHttpRequest();
    req->setParameter("keyword", long_keyword);

    auto result = SearchRequest::FromRequest(req);
    EXPECT_FALSE(result.has_value()) << "Keyword > 100 chars should fail";
    if (!result.has_value()) {
        EXPECT_EQ(result.error().code, ErrorCode::ValidationFailed);
    }
}

TEST(SearchRequest, KeywordForbiddenChars) {
    auto req = drogon::HttpRequest::newHttpRequest();
    req->setParameter("keyword", "test%keyword");

    auto result = SearchRequest::FromRequest(req);
    EXPECT_FALSE(result.has_value()) << "Keyword with % should fail";
    if (!result.has_value()) {
        EXPECT_EQ(result.error().code, ErrorCode::ValidationFailed);
    }
}

TEST(SearchRequest, KeywordForbiddenUnderscore) {
    auto req = drogon::HttpRequest::newHttpRequest();
    req->setParameter("keyword", "test_keyword");

    auto result = SearchRequest::FromRequest(req);
    EXPECT_FALSE(result.has_value()) << "Keyword with _ should fail";
    if (!result.has_value()) {
        EXPECT_EQ(result.error().code, ErrorCode::ValidationFailed);
    }
}

TEST(SearchRequest, InvalidType) {
    auto req = drogon::HttpRequest::newHttpRequest();
    req->setParameter("keyword", "test");
    req->setParameter("type", "document");

    auto result = SearchRequest::FromRequest(req);
    EXPECT_FALSE(result.has_value()) << "Invalid type should fail";
    if (!result.has_value()) {
        EXPECT_EQ(result.error().code, ErrorCode::ValidationFailed);
    }
}

TEST(SearchRequest, InvalidPage) {
    auto req = drogon::HttpRequest::newHttpRequest();
    req->setParameter("keyword", "test");
    req->setParameter("page", "0");

    auto result = SearchRequest::FromRequest(req);
    EXPECT_FALSE(result.has_value()) << "page = 0 should fail";
    if (!result.has_value()) {
        EXPECT_EQ(result.error().code, ErrorCode::ValidationFailed);
    }
}

TEST(SearchRequest, InvalidPageSizeTooLarge) {
    auto req = drogon::HttpRequest::newHttpRequest();
    req->setParameter("keyword", "test");
    req->setParameter("page_size", "101");

    auto result = SearchRequest::FromRequest(req);
    EXPECT_FALSE(result.has_value()) << "page_size > 100 should fail";
    if (!result.has_value()) {
        EXPECT_EQ(result.error().code, ErrorCode::ValidationFailed);
    }
}

/// ==================== SearchResultItem Tests ====================

TEST(SearchResultItem, ToJsonIncludesPath) {
    SearchResultItem item;
    item.id = 123;
    item.name = "report.pdf";
    item.type = "file";
    item.size = 2048576;
    item.mime_type = "application/pdf";
    item.hash = "abc123def456789012345678901234ab";
    item.created_at = "2026-02-14T10:30:00Z";
    item.updated_at = "2026-02-15T11:00:00Z";
    item.path = "/Documents/2026/report.pdf";

    auto json = item.ToJson();

    /// Inherited fields from FileListItem
    EXPECT_EQ(json["id"].asUInt64(), 123);
    EXPECT_EQ(json["name"].asString(), "report.pdf");
    EXPECT_EQ(json["type"].asString(), "file");
    EXPECT_EQ(json["size"].asUInt64(), 2048576);
    EXPECT_EQ(json["mime_type"].asString(), "application/pdf");
    EXPECT_EQ(json["hash"].asString(), "abc123def456789012345678901234ab");

    /// Search-specific field
    EXPECT_EQ(json["path"].asString(), "/Documents/2026/report.pdf");
}

TEST(SearchResultItem, ToJsonFolderWithPath) {
    SearchResultItem item;
    item.id = 456;
    item.name = "Projects";
    item.type = "folder";
    item.item_count = 10;
    item.created_at = "2026-02-14T10:30:00Z";
    item.updated_at = "2026-02-15T11:00:00Z";
    item.path = "/Documents/Projects";

    auto json = item.ToJson();

    EXPECT_EQ(json["id"].asUInt64(), 456);
    EXPECT_EQ(json["name"].asString(), "Projects");
    EXPECT_EQ(json["type"].asString(), "folder");
    EXPECT_EQ(json["item_count"].asInt(), 10);
    EXPECT_EQ(json["path"].asString(), "/Documents/Projects");
}

/// ==================== SearchResponse Tests ====================

TEST(SearchResponse, ToJsonWithPaginationMetadata) {
    SearchResponse response;

    SearchResultItem item1;
    item1.id = 1;
    item1.name = "doc1.pdf";
    item1.type = "file";
    item1.size = 1024;
    item1.mime_type = "application/pdf";
    item1.hash = "abc123def456789012345678901234ab";
    item1.created_at = "2026-02-14T10:30:00Z";
    item1.updated_at = "2026-02-14T10:30:00Z";
    item1.path = "/docs/doc1.pdf";

    SearchResultItem item2;
    item2.id = 2;
    item2.name = "folder1";
    item2.type = "folder";
    item2.item_count = 3;
    item2.created_at = "2026-02-13T10:30:00Z";
    item2.updated_at = "2026-02-14T11:00:00Z";
    item2.path = "/docs/folder1";

    response.items.push_back(item1);
    response.items.push_back(item2);

    response.pagination.page = 1;
    response.pagination.page_size = 20;
    response.pagination.total = 35;
    response.pagination.total_pages = 2;

    auto json = response.ToJson();

    /// Verify items array
    ASSERT_TRUE(json.isMember("items"));
    EXPECT_EQ(json["items"].size(), 2U);
    EXPECT_EQ(json["items"][0]["name"].asString(), "doc1.pdf");
    EXPECT_EQ(json["items"][0]["path"].asString(), "/docs/doc1.pdf");
    EXPECT_EQ(json["items"][1]["name"].asString(), "folder1");
    EXPECT_EQ(json["items"][1]["path"].asString(), "/docs/folder1");

    /// Verify pagination metadata
    ASSERT_TRUE(json.isMember("pagination"));
    EXPECT_EQ(json["pagination"]["page"].asInt(), 1);
    EXPECT_EQ(json["pagination"]["page_size"].asInt(), 20);
    EXPECT_EQ(json["pagination"]["total"].asInt(), 35);
    EXPECT_EQ(json["pagination"]["total_pages"].asInt(), 2);
}

TEST(SearchResponse, PaginationMetadataFieldNames) {
    /// Regression test: ensure search pagination field names match list pagination
    SearchResponse response;
    response.pagination.page = 1;
    response.pagination.page_size = 10;
    response.pagination.total = 100;
    response.pagination.total_pages = 10;

    auto json = response.ToJson();
    auto pagination = json["pagination"];

    /// Field names must match FileListResponse pagination
    EXPECT_TRUE(pagination.isMember("page"));
    EXPECT_TRUE(pagination.isMember("page_size"));
    EXPECT_TRUE(pagination.isMember("total"));
    EXPECT_TRUE(pagination.isMember("total_pages"));
}

TEST(SearchResponse, EmptySearchWithPagination) {
    SearchResponse response;
    response.pagination.page = 1;
    response.pagination.page_size = 20;
    response.pagination.total = 0;
    response.pagination.total_pages = 0;

    auto json = response.ToJson();

    EXPECT_TRUE(json["items"].isArray());
    EXPECT_EQ(json["items"].size(), 0U);
    EXPECT_EQ(json["pagination"]["total"].asInt(), 0);
    EXPECT_EQ(json["pagination"]["total_pages"].asInt(), 0);
}

/// ==================== Pagination Create Factory Tests ====================

TEST(Pagination, CreateFactoryCalculatesTotalPages) {
    /// Test the Pagination::Create factory method for correct total_pages calculation

    /// Case 1: Exact multiple
    auto p1 = Pagination::Create(1, 20, 100);
    EXPECT_EQ(p1.page, 1);
    EXPECT_EQ(p1.page_size, 20);
    EXPECT_EQ(p1.total, 100);
    EXPECT_EQ(p1.total_pages, 5);

    /// Case 2: Non-exact multiple (round up)
    auto p2 = Pagination::Create(1, 20, 45);
    EXPECT_EQ(p2.total_pages, 3); ///< ceil(45/20) = 3

    /// Case 3: Empty result
    auto p3 = Pagination::Create(1, 20, 0);
    EXPECT_EQ(p3.total_pages, 0);

    /// Case 4: Single item
    auto p4 = Pagination::Create(1, 20, 1);
    EXPECT_EQ(p4.total_pages, 1);

    /// Case 5: Items less than page_size
    auto p5 = Pagination::Create(1, 20, 15);
    EXPECT_EQ(p5.total_pages, 1);
}
