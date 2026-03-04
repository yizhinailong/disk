/**
 * @file OperationLogJsonDetails_test.cpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief Unit tests for logout operation log JSON details serialization round-trip
 * @version 0.1
 * @date 2026-03-02
 *
 * @copyright Copyright (c) 2026
 *
 * Validates that the JSON structure used for logout operation log details:
 * - Serializes correctly using Json::StreamWriterBuilder with no indentation
 * - Round-trips correctly (serialize then parse back yields the same value)
 * - Rejects invalid (non-JSON) strings during parse
 */

#include <string>

#include <gtest/gtest.h>
#include <json/json.h>
#include <json/reader.h>
#include <json/writer.h>

namespace {

    /**
     * @brief Validates that the logout details JSON object serializes and
     *        parses back correctly (round-trip).
     *
     * Production code constructs:
     *   { "message": "User logged out" }
     * using Json::StreamWriterBuilder with builder["indentation"] = "".
     *
     * This test replicates that exact pattern and verifies the round-trip.
     */
    TEST(OperationLogJsonDetails, RoundTrip) {
        // Build the same JSON as production code in AuthService::Logout
        Json::Value details_json;
        details_json["message"] = "User logged out";

        // Serialize using the project-standard compact serialization pattern
        Json::StreamWriterBuilder builder;
        builder["indentation"] = "";
        std::string serialized = Json::writeString(builder, details_json);

        // Verify the serialized string is valid compact JSON
        EXPECT_FALSE(serialized.empty()) << "Serialized JSON should not be empty";
        EXPECT_EQ(serialized, R"({"message":"User logged out"})") << "Compact JSON format mismatch";

        // Parse the serialized string back using Json::CharReaderBuilder
        Json::Value parsed;
        Json::CharReaderBuilder reader_builder;
        std::string parse_errors;
        std::istringstream iss(serialized);
        bool parse_ok = Json::parseFromStream(reader_builder, iss, &parsed, &parse_errors);

        ASSERT_TRUE(parse_ok) << "Parsing serialized JSON failed: " << parse_errors;
        EXPECT_TRUE(parse_errors.empty()) << "Expected no parse errors: " << parse_errors;

        // Assert the parsed value equals the original
        ASSERT_TRUE(parsed.isObject()) << "Parsed value should be a JSON object";
        ASSERT_TRUE(parsed.isMember("message")) << "Parsed object should have 'message' field";
        EXPECT_EQ(parsed["message"].asString(), "User logged out")
            << "Parsed 'message' field should match original";

        // Full object equality check
        EXPECT_EQ(parsed, details_json) << "Parsed value should equal original JSON object";
    }

    /**
     * @brief Validates that parsing the plain string "User logged out" (not
     *        valid JSON) fails, confirming that the fix in Task 1 was necessary.
     *
     * Before the fix, operation_logs.details was set to the raw string
     * "User logged out". A MySQL JSON column rejects such a value.
     * This test confirms that JsonCpp also correctly fails to parse it.
     */
    TEST(OperationLogJsonDetails, InvalidJsonFails) {
        const std::string invalid_json = "User logged out";

        Json::Value parsed;
        Json::CharReaderBuilder reader_builder;
        std::string parse_errors;
        std::istringstream iss(invalid_json);
        bool parse_ok = Json::parseFromStream(reader_builder, iss, &parsed, &parse_errors);

        EXPECT_FALSE(parse_ok) << "Parsing plain string as JSON should fail";
    }

} // namespace
