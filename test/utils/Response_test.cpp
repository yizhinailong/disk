/**
 * @file Response_test.cpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief Response 工具测试
 *
 * @copyright Copyright (c) 2026
 *
 */

#include "utils/Response.hpp"

#include <drogon/HttpResponse.h>
#include <drogon/HttpTypes.h>
#include <gtest/gtest.h>

#include "utils/ErrorCode.hpp"

using disk::Response;

TEST(Response, FromResultVoidSuccess) {
    Result<void> result;

    auto response = Response::FromResult(result);

    ASSERT_NE(response, nullptr) << "Response should not be null";
    EXPECT_EQ(response->getStatusCode(), drogon::k200OK) << "Status should be 200 OK";

    auto json = response->getJsonObject();
    ASSERT_NE(json, nullptr) << "JSON object should not be null";

    EXPECT_EQ((*json)["code"].asInt(), 0) << "Code should be 0 for success";
    EXPECT_EQ((*json)["message"].asString(), "success") << "Message should be success";
    EXPECT_TRUE((*json)["data"].isNull()) << "Data should be null for void result";
}

TEST(Response, FromResultVoidError) {
    Result<void> result = std::unexpected(ErrorInfo(ErrorCode::UsernameExists));

    auto response = Response::FromResult(result);

    ASSERT_NE(response, nullptr) << "Response should not be null";
    EXPECT_EQ(response->getStatusCode(), drogon::k400BadRequest)
        << "Status should match error code mapping";

    auto json = response->getJsonObject();
    ASSERT_NE(json, nullptr) << "JSON object should not be null";

    EXPECT_EQ((*json)["code"].asInt(), static_cast<int>(ErrorCode::UsernameExists))
        << "Code should match error code";
    EXPECT_EQ((*json)["message"].asString(), Error::GetErrorMessage(ErrorCode::UsernameExists))
        << "Message should match error code default";

    EXPECT_TRUE((*json)["data"].isNull()) << "Data should be null for error response";
}

TEST(Response, InternalErrorUsesStableEnvelope) {
    auto response = Response::Error(ErrorInfo(ErrorCode::InternalError));

    ASSERT_NE(response, nullptr) << "Response should not be null";
    EXPECT_EQ(response->getStatusCode(), drogon::k500InternalServerError)
        << "Status should be 500 for internal errors";

    auto json = response->getJsonObject();
    ASSERT_NE(json, nullptr) << "JSON object should not be null";

    EXPECT_EQ((*json)["code"].asInt(), static_cast<int>(ErrorCode::InternalError));
    EXPECT_EQ((*json)["message"].asString(), Error::GetErrorMessage(ErrorCode::InternalError));
    EXPECT_TRUE((*json)["data"].isNull()) << "Data should be null for error response";
}

TEST(Response, CustomDomainErrorEnvelopePreservesMessageAndStatus) {
    const auto error = ErrorInfo(ErrorCode::ValidationFailed, "domain validation failed");

    auto response = Response::Error(error);

    ASSERT_NE(response, nullptr) << "Response should not be null";
    EXPECT_EQ(response->getStatusCode(), drogon::k400BadRequest)
        << "Status should match domain error mapping";

    auto json = response->getJsonObject();
    ASSERT_NE(json, nullptr) << "JSON object should not be null";

    EXPECT_EQ((*json)["code"].asInt(), static_cast<int>(ErrorCode::ValidationFailed));
    EXPECT_EQ((*json)["message"].asString(), "domain validation failed");
    EXPECT_TRUE((*json)["data"].isNull()) << "Data should be null for error response";
}
