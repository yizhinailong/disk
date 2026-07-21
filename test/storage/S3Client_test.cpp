#include "storage/S3Client.hpp"

#include <array>
#include <memory>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>
#include <json/json.h>
#include <spdlog/sinks/ostream_sink.h>
#include <spdlog/spdlog.h>

namespace disk::storage {
    namespace {
        class S3ClientLogTest : public ::testing::Test {
        protected:
            auto SetUp() -> void override {
                m_previous_logger = spdlog::default_logger();
                m_sink = std::make_shared<spdlog::sinks::ostream_sink_mt>(m_output);
                m_logger = std::make_shared<spdlog::logger>("s3-client-test", m_sink);
                disk::utils::Logger::ApplyStructuredFormatter(m_logger);
                spdlog::set_default_logger(m_logger);
                disk::utils::Logger::SetInstanceId("s3-test-instance");
            }

            auto TearDown() -> void override {
                disk::utils::Logger::SetInstanceId("");
                spdlog::set_default_logger(m_previous_logger);
            }

            [[nodiscard]] auto Output() -> std::string {
                m_logger->flush();
                return m_output.str();
            }

            [[nodiscard]] auto Records() -> std::vector<Json::Value> {
                std::vector<Json::Value> records;
                std::istringstream lines(Output());
                std::string line;
                while (std::getline(lines, line)) {
                    if (line.empty()) {
                        continue;
                    }
                    Json::CharReaderBuilder builder;
                    builder["collectComments"] = false;
                    auto reader = std::unique_ptr<Json::CharReader>(builder.newCharReader());
                    Json::Value record;
                    std::string errors;
                    if (!reader->parse(
                            line.data(),
                            line.data() + line.size(),
                            &record,
                            &errors
                        )) {
                        ADD_FAILURE() << "Invalid structured log line: " << errors;
                        continue;
                    }
                    records.push_back(std::move(record));
                }
                return records;
            }

            std::shared_ptr<spdlog::logger> m_previous_logger;
            std::ostringstream m_output;
            std::shared_ptr<spdlog::sinks::ostream_sink_mt> m_sink;
            std::shared_ptr<spdlog::logger> m_logger;
        };
    } // namespace

    TEST(S3ClientRetryPolicyTest, RetriesTimeoutThrottleServerAndConnectionFailures) {
        EXPECT_EQ(ClassifyS3Failure(408, {}, false), S3FailureClass::Retryable);
        EXPECT_EQ(ClassifyS3Failure(429, {}, false), S3FailureClass::Retryable);
        EXPECT_EQ(ClassifyS3Failure(500, {}, false), S3FailureClass::Retryable);
        EXPECT_EQ(ClassifyS3Failure(503, {}, false), S3FailureClass::Retryable);
        EXPECT_EQ(ClassifyS3Failure(599, {}, false), S3FailureClass::Retryable);
        EXPECT_EQ(ClassifyS3Failure(-1, "NetworkConnection", false), S3FailureClass::Retryable);
        EXPECT_EQ(ClassifyS3Failure(-1, {}, true), S3FailureClass::Retryable);
    }

    TEST(S3ClientRetryPolicyTest, DoesNotRetryAuthenticationParameterOrOrdinaryClientFailures) {
        EXPECT_EQ(ClassifyS3Failure(401, {}, true), S3FailureClass::Permanent);
        EXPECT_EQ(ClassifyS3Failure(403, "AccessDenied", true), S3FailureClass::Permanent);
        EXPECT_EQ(ClassifyS3Failure(400, "InvalidArgument", true), S3FailureClass::Permanent);
        EXPECT_EQ(ClassifyS3Failure(403, "SignatureDoesNotMatch", true), S3FailureClass::Permanent);
        EXPECT_EQ(ClassifyS3Failure(404, "NoSuchBucket", true), S3FailureClass::Permanent);
        EXPECT_EQ(ClassifyS3Failure(409, {}, true), S3FailureClass::Permanent);
    }

    TEST(S3ClientRetryPolicyTest, StopsAtConfiguredRetryBudget) {
        EXPECT_TRUE(ShouldRetryS3Failure(503, {}, false, 0, 3));
        EXPECT_TRUE(ShouldRetryS3Failure(503, {}, false, 1, 3));
        EXPECT_TRUE(ShouldRetryS3Failure(503, {}, false, 2, 3));
        EXPECT_FALSE(ShouldRetryS3Failure(503, {}, false, 3, 3));
        EXPECT_FALSE(ShouldRetryS3Failure(503, {}, false, 0, 0));
        EXPECT_FALSE(ShouldRetryS3Failure(403, "AccessDenied", true, 0, 3));
    }

    TEST(S3ClientLogContractTest, UsesBoundedOperationAndOutcomeNames) {
        constexpr std::array operation_names{
            std::pair{       S3SdkOperation::GetBucketLocation,"get_bucket_location"                                                               },
            std::pair{              S3SdkOperation::HeadObject,             "head_object" },
            std::pair{               S3SdkOperation::PutObject,              "put_object" },
            std::pair{            S3SdkOperation::DeleteObject,           "delete_object" },
            std::pair{               S3SdkOperation::GetObject,              "get_object" },
            std::pair{           S3SdkOperation::ListObjectsV2,         "list_objects_v2" },
            std::pair{           S3SdkOperation::DeleteObjects,          "delete_objects" },
            std::pair{   S3SdkOperation::CreateMultipartUpload, "create_multipart_upload" },
            std::pair{              S3SdkOperation::UploadPart,             "upload_part" },
            std::pair{          S3SdkOperation::UploadPartCopy,        "upload_part_copy" },
            std::pair{ S3SdkOperation::CompleteMultipartUpload,
                      "complete_multipart_upload"                                        },
            std::pair{    S3SdkOperation::AbortMultipartUpload,  "abort_multipart_upload" },
        };
        for (const auto& [operation, name] : operation_names) {
            EXPECT_EQ(S3SdkOperationName(operation), name);
        }

        constexpr std::array outcome_names{
            std::pair{    S3SdkOutcome::Success,    "success" },
            std::pair{    S3SdkOutcome::Timeout,    "timeout" },
            std::pair{ S3SdkOutcome::Connection, "connection" },
            std::pair{   S3SdkOutcome::Conflict,   "conflict" },
            std::pair{   S3SdkOutcome::NotFound,  "not_found" },
            std::pair{  S3SdkOutcome::Retryable,  "retryable" },
            std::pair{  S3SdkOutcome::Permanent,  "permanent" },
            std::pair{   S3SdkOutcome::Protocol,   "protocol" },
            std::pair{      S3SdkOutcome::Other,      "other" },
        };
        for (const auto& [outcome, name] : outcome_names) {
            EXPECT_EQ(S3SdkOutcomeName(outcome), name);
        }
    }

    TEST_F(S3ClientLogTest, InfoLevelDropsExpectedResultsAndCorrelatesFailures) {
        m_logger->set_level(spdlog::level::info);
        const disk::utils::LogContext log_context{
            .request_id = "request-123",
            .operation = "upload_complete",
            .upload_id = "upload-456",
            .job_id = 42,
            .lease_owner = "worker-7",
            .state_version = 9,
        };

        RecordS3SdkCallResult(log_context, S3SdkOperation::PutObject, S3SdkOutcome::Success);
        RecordS3SdkCallResult(log_context, S3SdkOperation::HeadObject, S3SdkOutcome::NotFound);
        RecordS3SdkCallResult(log_context, S3SdkOperation::PutObject, S3SdkOutcome::Conflict);
        RecordS3SdkCallResult(log_context, S3SdkOperation::GetObject, S3SdkOutcome::Timeout);

        const auto records = Records();
        ASSERT_EQ(records.size(), 1U);
        const auto& record = records.front();
        EXPECT_EQ(record["level"].asString(), "warning");
        EXPECT_EQ(
            record["message"].asString(),
            "S3 SDK call result: sdk_operation=get_object, outcome=timeout"
        );
        EXPECT_EQ(record["request_id"].asString(), "request-123");
        EXPECT_EQ(record["operation"].asString(), "upload_complete");
        EXPECT_EQ(record["upload_id"].asString(), "upload-456");
        EXPECT_EQ(record["job_id"].asUInt64(), 42U);
        EXPECT_EQ(record["lease_owner"].asString(), "worker-7");
        EXPECT_EQ(record["state_version"].asUInt64(), 9U);
        EXPECT_EQ(Output().find("secret-bucket/private-object-key"), std::string::npos);
    }

    TEST_F(S3ClientLogTest, DebugLevelKeepsExpectedResults) {
        m_logger->set_level(spdlog::level::debug);

        RecordS3SdkCallResult(
            disk::utils::LogContext{ .operation = "file_download" },
            S3SdkOperation::GetObject,
            S3SdkOutcome::Success
        );

        const auto records = Records();
        ASSERT_EQ(records.size(), 1U);
        EXPECT_EQ(records.front()["level"].asString(), "debug");
        EXPECT_EQ(records.front()["operation"].asString(), "file_download");
        EXPECT_EQ(
            records.front()["message"].asString(),
            "S3 SDK call result: sdk_operation=get_object, outcome=success"
        );
    }

} // namespace disk::storage
