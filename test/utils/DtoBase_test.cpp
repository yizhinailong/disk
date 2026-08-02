/**
 * @file DtoBase_test.cpp
 * @brief Shared DTO validation and serialization contract tests
 *
 * @copyright Copyright (c) 2026
 */

#include "utils/DtoBase.hpp"

#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>

#include <gtest/gtest.h>
#include <json/json.h>
#include <spdlog/logger.h>
#include <spdlog/sinks/ostream_sink.h>
#include <spdlog/spdlog.h>

#include "utils/LogHelper.hpp"

namespace disk::test {
    namespace {

        class DtoBaseProbe final : public DtoBase<DtoBaseProbe> {
        public:
            using DtoBase<DtoBaseProbe>::OptionalBool;
            using DtoBase<DtoBaseProbe>::OptionalPositiveIdArray;
            using DtoBase<DtoBaseProbe>::OptionalString;
            using DtoBase<DtoBaseProbe>::OptionalUInt64;
            using DtoBase<DtoBaseProbe>::ParsePositiveUInt64;
            using DtoBase<DtoBaseProbe>::QueryPositiveInt;
            using DtoBase<DtoBaseProbe>::QueryUInt64;
            using DtoBase<DtoBaseProbe>::RequireInt;
            using DtoBase<DtoBaseProbe>::RequireJsonBody;
            using DtoBase<DtoBaseProbe>::RequirePositiveIdArray;
            using DtoBase<DtoBaseProbe>::RequireString;
            using DtoBase<DtoBaseProbe>::RequireUInt64;
        };

        class ScopedLogCapture final {
        public:
            ScopedLogCapture()
                : m_previous_logger(spdlog::default_logger()),
                  m_sink(std::make_shared<spdlog::sinks::ostream_sink_mt>(m_output)),
                  m_logger(std::make_shared<spdlog::logger>("dto-base-test", m_sink)) {
                m_logger->set_level(spdlog::level::trace);
                Logger::ApplyStructuredFormatter(m_logger);
                spdlog::set_default_logger(m_logger);
                Logger::CaptureFrameworkLogs();
            }

            ~ScopedLogCapture() {
                spdlog::set_default_logger(m_previous_logger);
                Logger::CaptureFrameworkLogs();
            }

            ScopedLogCapture(const ScopedLogCapture&) = delete;
            auto operator=(const ScopedLogCapture&) -> ScopedLogCapture& = delete;

            [[nodiscard]] auto Output() -> std::string {
                m_logger->flush();
                return m_output.str();
            }

        private:
            std::shared_ptr<spdlog::logger> m_previous_logger;
            std::ostringstream m_output;
            std::shared_ptr<spdlog::sinks::ostream_sink_mt> m_sink;
            std::shared_ptr<spdlog::logger> m_logger;
        };

        template <typename T>
        auto ExpectError(
            const Result<T>& result,
            ErrorCode code,
            std::string_view message
        ) -> void {
            ASSERT_FALSE(result.has_value());
            EXPECT_EQ(result.error().code, code);
            EXPECT_EQ(result.error().message, message);
        }

        [[nodiscard]] auto InvalidJsonRequest() -> drogon::HttpRequestPtr {
            auto request = drogon::HttpRequest::newHttpRequest();
            request->setBody("{");
            request->setContentTypeCode(drogon::CT_APPLICATION_JSON);
            return request;
        }

        [[nodiscard]] auto ReadSourceFile(const std::filesystem::path& relative_path)
            -> std::string {
            const auto root = std::filesystem::path(__FILE__)
                                  .parent_path()
                                  .parent_path()
                                  .parent_path();
            std::ifstream input(root / relative_path);
            std::ostringstream buffer;
            buffer << input.rdbuf();
            return buffer.str();
        }

        [[nodiscard]] auto CountOccurrences(
            const std::string& source,
            std::string_view expected
        ) -> size_t {
            size_t count = 0;
            size_t position = 0;
            while ((position = source.find(expected, position)) != std::string::npos) {
                ++count;
                position += expected.size();
            }
            return count;
        }

        TEST(DtoBaseTest, ValidationErrorsArePureResults) {
            const auto source = ReadSourceFile("src/utils/DtoBase.hpp");
            ASSERT_FALSE(source.empty());
            EXPECT_EQ(source.find("Logger::"), std::string::npos);
            EXPECT_EQ(source.find("LogHelper.hpp"), std::string::npos);
            EXPECT_EQ(source.find("static auto RequireBool("), std::string::npos);
            EXPECT_EQ(source.find("static auto OptionalInt("), std::string::npos);
            EXPECT_EQ(source.find("#include <string_view>"), std::string::npos);
            EXPECT_EQ(source.find("std::string_view value"), std::string::npos);
            EXPECT_EQ(source.find("const char* value"), std::string::npos);
            EXPECT_EQ(source.find("const std::vector<uint64_t>& items"), std::string::npos);
            EXPECT_EQ(source.find("const std::vector<std::string>& items"), std::string::npos);

            ScopedLogCapture capture;

            ExpectError(
                DtoBaseProbe::RequireJsonBody(InvalidJsonRequest()),
                ErrorCode::ValidationFailed,
                "Request body is not valid JSON"
            );

            Json::Value fields(Json::objectValue);
            fields["string"] = 7;
            fields["uint"] = -1;
            fields["int"] = "not-an-integer";
            fields["bool"] = "not-a-boolean";
            ExpectError(
                DtoBaseProbe::RequireString(fields, "missing"),
                ErrorCode::ValidationFailed,
                "Missing required parameter: missing"
            );
            ExpectError(
                DtoBaseProbe::RequireUInt64(fields, "uint"),
                ErrorCode::ValidationFailed,
                "Parameter 'uint' type error: expected unsigned integer"
            );
            ExpectError(
                DtoBaseProbe::RequireInt(fields, "int"),
                ErrorCode::ValidationFailed,
                "Parameter 'int' type error: expected integer"
            );
            ExpectError(
                DtoBaseProbe::OptionalString(fields, "string"),
                ErrorCode::ValidationFailed,
                "Parameter 'string' type error: expected string"
            );
            ExpectError(
                DtoBaseProbe::OptionalUInt64(fields, "uint"),
                ErrorCode::ValidationFailed,
                "Parameter 'uint' type error: expected unsigned integer"
            );
            ExpectError(
                DtoBaseProbe::OptionalBool(fields, "bool"),
                ErrorCode::ValidationFailed,
                "Parameter 'bool' type error: expected boolean"
            );

            ExpectError(
                DtoBaseProbe::ParsePositiveUInt64("raw-path-secret", "resource_id"),
                ErrorCode::InvalidParameter,
                "Parameter 'resource_id' invalid format"
            );
            auto query = drogon::HttpRequest::newHttpRequest();
            query->setParameter("page", "raw-query-secret");
            query->setParameter("cursor", "invalid-cursor");
            ExpectError(
                DtoBaseProbe::QueryPositiveInt(query, "page", 1, 100),
                ErrorCode::ValidationFailed,
                "Parameter 'page' invalid format"
            );
            ExpectError(
                DtoBaseProbe::QueryUInt64(query, "cursor"),
                ErrorCode::ValidationFailed,
                "Parameter 'cursor' invalid format"
            );

            Json::Value optional_ids(Json::objectValue);
            optional_ids["ids"] = "not-an-array";
            ExpectError(
                DtoBaseProbe::OptionalPositiveIdArray(optional_ids, "ids"),
                ErrorCode::InvalidParameter,
                "Parameter 'ids' type error: expected array"
            );

            Json::Value empty_ids(Json::objectValue);
            empty_ids["ids"] = Json::Value(Json::arrayValue);
            ExpectError(
                DtoBaseProbe::RequirePositiveIdArray(empty_ids, "ids"),
                ErrorCode::ValidationFailed,
                "Parameter 'ids' cannot be empty array"
            );

            Json::Value too_many_ids(Json::objectValue);
            too_many_ids["ids"] = Json::Value(Json::arrayValue);
            for (Json::UInt64 id = 1; id <= 101; ++id) {
                too_many_ids["ids"].append(id);
            }
            ExpectError(
                DtoBaseProbe::RequirePositiveIdArray(too_many_ids, "ids"),
                ErrorCode::ValidationFailed,
                "Parameter 'ids' supports at most 100 IDs"
            );

            EXPECT_TRUE(capture.Output().empty());
        }

        TEST(DtoBaseSourceContractTest, RequestDtosShareWhitespaceTrimming) {
            const auto base = ReadSourceFile("src/utils/DtoBase.hpp");
            const auto storage_job = ReadSourceFile("src/dtos/StorageJobAdminDto.hpp");
            const auto storage_recovery =
                ReadSourceFile("src/dtos/StorageRecoveryAdminDto.hpp");
            const auto user = ReadSourceFile("src/dtos/UserDto.hpp");

            EXPECT_EQ(CountOccurrences(base, "static auto TrimWhitespace("), 1U);
            EXPECT_NE(base.find("[](unsigned char character)"), std::string::npos);
            EXPECT_NE(base.find("std::isspace(character)"), std::string::npos);

            EXPECT_EQ(CountOccurrences(storage_job, "TrimWhitespace("), 1U);
            EXPECT_EQ(storage_job.find("static auto Trim("), std::string::npos);
            EXPECT_EQ(storage_job.find("#include <cctype>"), std::string::npos);

            EXPECT_EQ(CountOccurrences(storage_recovery, "TrimWhitespace("), 1U);
            EXPECT_EQ(storage_recovery.find("static auto Trim("), std::string::npos);
            EXPECT_EQ(storage_recovery.find("#include <cctype>"), std::string::npos);

            EXPECT_EQ(CountOccurrences(user, "TrimWhitespace("), 2U);
            EXPECT_EQ(user.find("static auto TrimWhitespace("), std::string::npos);
            EXPECT_EQ(user.find("#include <cctype>"), std::string::npos);
        }

    } // namespace
} // namespace disk::test
