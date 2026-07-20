#include "dtos/UploadDiagnosticDto.hpp"

#include <map>
#include <string>

#include <drogon/HttpRequest.h>
#include <gtest/gtest.h>

namespace disk::admin {
    namespace {
        auto QueryRequest(std::map<std::string, std::string> values)
            -> drogon::HttpRequestPtr {
            auto request = drogon::HttpRequest::newHttpRequest();
            for (auto& [key, value] : values) {
                request->setParameter(key, value);
            }
            return request;
        }

        TEST(UploadDiagnosticRequestTest, AcceptsSafeIdAndDefaultPagination) {
            auto parsed = UploadDiagnosticRequest::FromRequest(
                QueryRequest({}),
                "upload.2026_07-20:abc"
            );

            ASSERT_TRUE(parsed.has_value());
            EXPECT_EQ(parsed->upload_id, "upload.2026_07-20:abc");
            EXPECT_EQ(parsed->chunk_page, 1);
            EXPECT_EQ(parsed->chunk_page_size, 20);
            EXPECT_EQ(parsed->job_page, 1);
            EXPECT_EQ(parsed->job_page_size, 20);
        }

        TEST(UploadDiagnosticRequestTest, AcceptsIndependentBoundedPagination) {
            auto parsed = UploadDiagnosticRequest::FromRequest(
                QueryRequest({
                    {      "chunk_page",   "3" },
                    { "chunk_page_size", "100" },
                    {        "job_page",   "4" },
                    {   "job_page_size",   "5" },
            }),
                "upload-123"
            );

            ASSERT_TRUE(parsed.has_value());
            EXPECT_EQ(parsed->chunk_page, 3);
            EXPECT_EQ(parsed->chunk_page_size, 100);
            EXPECT_EQ(parsed->job_page, 4);
            EXPECT_EQ(parsed->job_page_size, 5);
        }

        TEST(UploadDiagnosticRequestTest, RejectsUnsafeIdsAndInvalidPagination) {
            EXPECT_FALSE(UploadDiagnosticRequest::FromRequest(QueryRequest({}), "").has_value());
            EXPECT_FALSE(
                UploadDiagnosticRequest::FromRequest(QueryRequest({}), std::string(65, 'a'))
                    .has_value()
            );
            EXPECT_FALSE(
                UploadDiagnosticRequest::FromRequest(QueryRequest({}), "../upload").has_value()
            );
            EXPECT_FALSE(
                UploadDiagnosticRequest::FromRequest(QueryRequest({}), "upload/key").has_value()
            );
            EXPECT_FALSE(
                UploadDiagnosticRequest::FromRequest(
                    QueryRequest({
                        { "chunk_page", "0" }
            }),
                    "upload-123"
                )
                    .has_value()
            );
            EXPECT_FALSE(
                UploadDiagnosticRequest::FromRequest(
                    QueryRequest({
                        { "job_page_size", "101" }
            }),
                    "upload-123"
                )
                    .has_value()
            );
        }

        TEST(UploadDiagnosticResponseTest, SerializesLeaseHeadAndPayloadFreeRelatedJobs) {
            UploadDiagnosticResponse response;
            response.task.upload_id = "upload-123";
            response.task.staging_backend = disk::storage::UploadStagingBackend::S3;
            response.task.staging_prefix = "staging/upload-123";
            response.task.status = disk::upload::UploadTaskStatus::Finalizing;
            response.task.state_version = 7;
            response.task.lease = UploadDiagnosticLease{
                .owner = "api-a",
                .expires_at = "2026-07-20 12:00:00",
                .expired = false,
            };
            response.chunks.push_back(UploadDiagnosticChunk{
                .chunk_index = 0,
                .size_bytes = 4,
                .hash_md5 = "8d777f385d3dfec8815d20f7496026dc",
                .object_key = "staging/upload-123/chunks/0-8d777f385d3dfec8815d20f7496026dc.part",
                .etag = "\"etag\"",
                .uploaded_at = "2026-07-20 11:59:00",
                .object_head = UploadDiagnosticObjectHead{
                                                          .status = "present",
                                                          .size_bytes = 4,
                                                          .etag = "\"etag\"",
                                                          .matches_record = true,
                                                          },
            });
            response.chunk_total = 1;
            response.chunk_total_pages = 1;
            response.related_jobs.items.push_back(StorageJobItem{
                .id = 9,
                .job_type = "staging_cleanup",
                .aggregate_id = "upload-123",
                .dedupe_key = "cleanup:upload-123",
                .payload = Json::Value(Json::objectValue),
            });
            response.related_jobs.total = 1;
            response.related_jobs.total_pages = 1;

            const auto json = response.ToJson();

            EXPECT_EQ(json["task"]["status"].asString(), "finalizing");
            EXPECT_EQ(json["task"]["staging_backend"].asString(), "s3");
            EXPECT_EQ(json["task"]["lease"]["owner"].asString(), "api-a");
            EXPECT_EQ(json["chunks"][0]["object_head"]["status"].asString(), "present");
            EXPECT_TRUE(json["chunks"][0]["object_head"]["matches_record"].asBool());
            EXPECT_TRUE(json["chunks"][0]["object_head"]["error_code"].isNull());
            EXPECT_EQ(json["chunk_pagination"]["total"].asUInt64(), 1U);
            EXPECT_EQ(json["related_jobs"]["items"][0]["id"].asUInt64(), 9U);
            EXPECT_FALSE(json["related_jobs"]["items"][0].isMember("payload"));
        }
    } // namespace
} // namespace disk::admin
