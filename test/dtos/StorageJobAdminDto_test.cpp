#include "dtos/StorageJobAdminDto.hpp"

#include <map>
#include <string>

#include <drogon/HttpRequest.h>
#include <gtest/gtest.h>
#include <json/json.h>

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

        auto JsonRequest(const Json::Value& value) -> drogon::HttpRequestPtr {
            Json::StreamWriterBuilder builder;
            builder["indentation"] = "";
            auto request = drogon::HttpRequest::newHttpRequest();
            request->setBody(Json::writeString(builder, value));
            request->setContentTypeCode(drogon::CT_APPLICATION_JSON);
            return request;
        }

        TEST(StorageJobListRequestTest, DefaultsToDeadLetterAndBoundedPagination) {
            auto parsed = StorageJobListRequest::FromRequest(QueryRequest({}));

            ASSERT_TRUE(parsed.has_value());
            EXPECT_EQ(parsed->status, disk::jobs::StorageJobStatus::DeadLetter);
            EXPECT_FALSE(parsed->job_type.has_value());
            EXPECT_EQ(parsed->page, 1);
            EXPECT_EQ(parsed->page_size, 20);
        }

        TEST(StorageJobListRequestTest, AcceptsKnownFilters) {
            auto parsed = StorageJobListRequest::FromRequest(QueryRequest({
                {    "status",   "retry" },
                {  "job_type", "blob_gc" },
                {      "page",       "3" },
                { "page_size",     "100" },
            }));

            ASSERT_TRUE(parsed.has_value());
            EXPECT_EQ(parsed->status, disk::jobs::StorageJobStatus::Retry);
            EXPECT_EQ(parsed->job_type, disk::jobs::kBlobGcJobType);
            EXPECT_EQ(parsed->page, 3);
            EXPECT_EQ(parsed->page_size, 100);
        }

        TEST(StorageJobListRequestTest, RejectsUnknownFiltersAndOversizedPage) {
            EXPECT_FALSE(StorageJobListRequest::FromRequest(QueryRequest({
                                                                { "status", "failed" },
            }))
                             .has_value());
            EXPECT_FALSE(StorageJobListRequest::FromRequest(QueryRequest({
                                                                { "job_type", "arbitrary" },
            }))
                             .has_value());
            EXPECT_FALSE(StorageJobListRequest::FromRequest(QueryRequest({
                                                                { "page_size", "101" },
            }))
                             .has_value());
        }

        TEST(StorageJobReplayRequestTest, DryRunIsDefaultAndNeedsNoConfirmation) {
            auto parsed = StorageJobReplayRequest::FromRequest(
                JsonRequest(Json::Value(Json::objectValue)),
                42
            );

            ASSERT_TRUE(parsed.has_value());
            EXPECT_TRUE(parsed->dry_run);
            EXPECT_FALSE(parsed->confirm_job_id.has_value());
            EXPECT_TRUE(parsed->reason.empty());
        }

        TEST(StorageJobReplayRequestTest, ActualReplayRequiresExactIdAndTrimmedReason) {
            Json::Value body(Json::objectValue);
            body["dry_run"] = false;
            body["confirm_job_id"] = Json::UInt64(42);
            body["reason"] = "\t dependency recovered \n";

            auto parsed = StorageJobReplayRequest::FromRequest(JsonRequest(body), 42);

            ASSERT_TRUE(parsed.has_value());
            EXPECT_FALSE(parsed->dry_run);
            EXPECT_EQ(parsed->confirm_job_id, 42U);
            EXPECT_EQ(parsed->reason, "dependency recovered");
        }

        TEST(StorageJobReplayRequestTest, RejectsUnsafeActualReplayContracts) {
            Json::Value missing_reason(Json::objectValue);
            missing_reason["dry_run"] = false;
            missing_reason["confirm_job_id"] = Json::UInt64(42);
            EXPECT_FALSE(StorageJobReplayRequest::FromRequest(JsonRequest(missing_reason), 42).has_value());

            auto whitespace_reason = missing_reason;
            whitespace_reason["reason"] = "\t \n";
            EXPECT_FALSE(
                StorageJobReplayRequest::FromRequest(
                    JsonRequest(whitespace_reason),
                    42
                )
                    .has_value()
            );

            Json::Value mismatched(Json::objectValue);
            mismatched["dry_run"] = false;
            mismatched["confirm_job_id"] = Json::UInt64(41);
            mismatched["reason"] = "retry";
            EXPECT_FALSE(StorageJobReplayRequest::FromRequest(JsonRequest(mismatched), 42).has_value());

            Json::Value unknown(Json::objectValue);
            unknown["force"] = true;
            EXPECT_FALSE(StorageJobReplayRequest::FromRequest(JsonRequest(unknown), 42).has_value());
        }

        TEST(StorageJobReplayRequestTest, ParsesOnlyPositivePathIds) {
            EXPECT_EQ(StorageJobReplayRequest::ParseJobId("42"), 42U);
            EXPECT_FALSE(StorageJobReplayRequest::ParseJobId("0").has_value());
            EXPECT_FALSE(StorageJobReplayRequest::ParseJobId("42x").has_value());
        }

        TEST(StorageJobItemTest, ListShapeOmitsPayloadAndDetailIncludesIt) {
            StorageJobItem item;
            item.id = 7;
            item.job_type = "blob_gc";
            item.payload["content_id"] = Json::UInt64(9);

            const auto list_json = item.ToJson();
            const auto detail_json = item.ToJson(true);

            EXPECT_FALSE(list_json.isMember("payload"));
            ASSERT_TRUE(detail_json.isMember("payload"));
            EXPECT_EQ(detail_json["payload"]["content_id"].asUInt64(), 9U);
        }
    } // namespace
} // namespace disk::admin
