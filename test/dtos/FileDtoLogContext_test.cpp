/**
 * @file FileDtoLogContext_test.cpp
 * @brief File DTO request correlation contract tests
 *
 * @copyright Copyright (c) 2026
 */

#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>
#include <json/json.h>
#include <spdlog/sinks/ostream_sink.h>
#include <spdlog/spdlog.h>

#include "dtos/FileDto.hpp"
#include "utils/LogHelper.hpp"

namespace disk::file {
    namespace {

        auto RepositoryRoot() -> std::filesystem::path {
            return std::filesystem::path(__FILE__).parent_path().parent_path().parent_path();
        }

        auto ReadSourceFile(const std::filesystem::path& relative_path) -> std::string {
            std::ifstream input(RepositoryRoot() / relative_path);
            std::ostringstream buffer;
            buffer << input.rdbuf();
            return buffer.str();
        }

        auto Contains(const std::string& source, std::string_view expected) -> bool {
            return source.find(expected) != std::string::npos;
        }

        auto CountOccurrences(const std::string& source, std::string_view expected) -> size_t {
            size_t count = 0;
            size_t position = 0;
            while ((position = source.find(expected, position)) != std::string::npos) {
                ++count;
                position += expected.size();
            }
            return count;
        }

        auto CreateJsonRequest(const Json::Value& json) -> drogon::HttpRequestPtr {
            Json::StreamWriterBuilder builder;
            builder["indentation"] = "";

            auto request = drogon::HttpRequest::newHttpRequest();
            request->setBody(Json::writeString(builder, json));
            request->setContentTypeCode(drogon::CT_APPLICATION_JSON);
            return request;
        }

        class FileDtoLogContextTest : public ::testing::Test {
        protected:
            auto SetUp() -> void override {
                m_previous_logger = spdlog::default_logger();
                m_sink = std::make_shared<spdlog::sinks::ostream_sink_mt>(m_output);
                m_logger = std::make_shared<spdlog::logger>("file-dto-context-test", m_sink);
                m_logger->set_level(spdlog::level::debug);
                disk::utils::Logger::ApplyStructuredFormatter(m_logger);
                spdlog::set_default_logger(m_logger);
                disk::utils::Logger::SetInstanceId("file-dto-instance");
            }

            auto TearDown() -> void override {
                disk::utils::Logger::SetInstanceId("");
                spdlog::set_default_logger(m_previous_logger);
            }

            [[nodiscard]] auto DrainRecords() -> std::vector<Json::Value> {
                m_logger->flush();

                std::vector<Json::Value> records;
                std::istringstream lines(m_output.str());
                std::string line;
                while (std::getline(lines, line)) {
                    if (line.empty()) {
                        continue;
                    }

                    Json::CharReaderBuilder builder;
                    builder["collectComments"] = false;
                    const auto reader = std::unique_ptr<Json::CharReader>(builder.newCharReader());
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

                m_output.str("");
                m_output.clear();
                return records;
            }

            static auto ExpectFullContext(const Json::Value& record) -> void {
                EXPECT_EQ(record["request_id"].asString(), "file-dto-request");
                EXPECT_EQ(record["instance_id"].asString(), "file-dto-instance");
                EXPECT_EQ(record["operation"].asString(), "file_mutation");
                EXPECT_EQ(record["upload_id"].asString(), "caller-upload");
                EXPECT_EQ(record["job_id"].asUInt64(), 71U);
                EXPECT_EQ(record["lease_owner"].asString(), "caller-owner");
                EXPECT_EQ(record["state_version"].asUInt64(), 13U);
            }

            std::shared_ptr<spdlog::logger> m_previous_logger;
            std::ostringstream m_output;
            std::shared_ptr<spdlog::sinks::ostream_sink_mt> m_sink;
            std::shared_ptr<spdlog::logger> m_logger;
        };

        TEST(FileDtoLogContextContractTest, EveryDirectLoggingParserReceivesControllerContext) {
            const auto controller_source = ReadSourceFile("src/controllers/FileController.cpp");
            const auto dto_source = ReadSourceFile("src/dtos/FileDto.hpp");

            ASSERT_FALSE(controller_source.empty());
            ASSERT_FALSE(dto_source.empty());
            EXPECT_EQ(
                CountOccurrences(dto_source, "disk::utils::LogContext log_context = {}"),
                10
            );
            EXPECT_EQ(CountOccurrences(dto_source, "Logger::Debug(log_context)"), 23);
            EXPECT_EQ(CountOccurrences(dto_source, "Logger::Warn(log_context)"), 24);
            EXPECT_FALSE(Contains(dto_source, "Logger::Debug()"));
            EXPECT_FALSE(Contains(dto_source, "Logger::Warn()"));
            EXPECT_FALSE(Contains(dto_source, "GetRequestLogContext"));
            EXPECT_FALSE(Contains(dto_source, "log_context.upload_id"));
            EXPECT_FALSE(Contains(dto_source, "log_context.job_id"));
            EXPECT_FALSE(Contains(dto_source, "log_context.lease_owner"));
            EXPECT_FALSE(Contains(dto_source, "log_context.state_version"));

            EXPECT_EQ(
                CountOccurrences(
                    controller_source,
                    "DownloadInfoRequest::FromPath(file_id, log_context)"
                ),
                2
            );
            for (const auto* call : {
                     "InitUploadRequest::FromRequest(request, log_context)",
                     "CompleteUploadRequest::FromRequest(request, log_context)",
                     "FileListRequest::FromRequest(request, log_context)",
                     "DownloadRequest::FromPath(file_id, log_context)",
                     "RenameRequest::FromPathAndRequest(file_id, request, log_context)",
                     "MoveRequest::FromRequest(request, log_context)",
                     "CopyRequest::FromRequest(request, log_context)",
                     "DeleteRequest::FromRequest(request, log_context)",
                     "SearchRequest::FromRequest(request, log_context)",
                 }) {
                EXPECT_TRUE(Contains(controller_source, call)) << call;
            }
        }

        TEST_F(FileDtoLogContextTest, PreservesExplicitContextAndNullDefaultsWithoutInference) {
            Json::Value invalid_init;
            invalid_init["filename"] = "context-test.txt";
            invalid_init["file_size"] = Json::UInt64(1024);
            invalid_init["file_hash"] = "invalid-hash";
            invalid_init["parent_id"] = Json::UInt64(0);

            const disk::utils::LogContext full_context{
                .request_id = "file-dto-request",
                .operation = "file_mutation",
                .upload_id = "caller-upload",
                .job_id = 71,
                .lease_owner = "caller-owner",
                .state_version = 13,
            };
            const auto invalid_result =
                InitUploadRequest::FromRequest(CreateJsonRequest(invalid_init), full_context);
            ASSERT_FALSE(invalid_result.has_value());

            const auto explicit_records = DrainRecords();
            ASSERT_EQ(explicit_records.size(), 3U);
            for (const auto& record : explicit_records) {
                ExpectFullContext(record);
            }

            Json::Value complete;
            complete["upload_id"] = "request-upload-must-not-be-inferred";
            const auto complete_result = CompleteUploadRequest::FromRequest(
                CreateJsonRequest(complete),
                disk::utils::LogContext{
                    .request_id = "complete-dto-request",
                    .operation = "upload_complete",
                }
            );
            ASSERT_TRUE(complete_result.has_value());

            const auto complete_records = DrainRecords();
            ASSERT_EQ(complete_records.size(), 3U);
            for (const auto& record : complete_records) {
                EXPECT_EQ(record["request_id"].asString(), "complete-dto-request");
                EXPECT_EQ(record["instance_id"].asString(), "file-dto-instance");
                EXPECT_EQ(record["operation"].asString(), "upload_complete");
                for (const auto* field : {
                         "upload_id",
                         "job_id",
                         "lease_owner",
                         "state_version",
                     }) {
                    EXPECT_TRUE(record[field].isNull()) << field;
                }
            }

            const auto default_result = DownloadInfoRequest::FromPath("-9");
            ASSERT_FALSE(default_result.has_value());

            const auto default_records = DrainRecords();
            ASSERT_EQ(default_records.size(), 2U);
            for (const auto& record : default_records) {
                EXPECT_EQ(record["instance_id"].asString(), "file-dto-instance");
                for (const auto* field : {
                         "request_id",
                         "operation",
                         "upload_id",
                         "job_id",
                         "lease_owner",
                         "state_version",
                     }) {
                    EXPECT_TRUE(record[field].isNull()) << field;
                }
            }
        }

    } // namespace
} // namespace disk::file
