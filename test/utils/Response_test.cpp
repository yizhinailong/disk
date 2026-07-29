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

struct IntToJson {
    auto operator()(int value) const -> Json::Value {
        return Json::Value(value);
    }
};

template <typename ResponseFactory>
concept HasValueFromResult = requires(const Result<int>& result) {
    ResponseFactory::FromResult(result, IntToJson{});
};

template <typename ResponseFactory>
concept HasVoidFromResult = requires(const Result<void>& result) {
    ResponseFactory::FromResult(result);
};

static_assert(!HasValueFromResult<Response>);
static_assert(!HasVoidFromResult<Response>);

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

TEST(Response, UploadTaskCreationDisabledUsesServiceUnavailableEnvelope) {
    auto response = Response::Error(ErrorInfo(ErrorCode::UploadTaskCreationDisabled));

    ASSERT_NE(response, nullptr);
    EXPECT_EQ(response->getStatusCode(), drogon::k503ServiceUnavailable);

    auto json = response->getJsonObject();
    ASSERT_NE(json, nullptr);
    EXPECT_EQ((*json)["code"].asInt(), 50012);
    EXPECT_EQ(
        (*json)["message"].asString(),
        "New upload task creation is temporarily disabled"
    );
    EXPECT_TRUE((*json)["data"].isNull());
}

TEST(Response, UploadLifecycleFrozenUsesServiceUnavailableEnvelope) {
    auto response = Response::Error(ErrorInfo(ErrorCode::UploadLifecycleFrozen));

    ASSERT_NE(response, nullptr);
    EXPECT_EQ(response->getStatusCode(), drogon::k503ServiceUnavailable);

    auto json = response->getJsonObject();
    ASSERT_NE(json, nullptr);
    EXPECT_EQ((*json)["code"].asInt(), 50013);
    EXPECT_EQ(
        (*json)["message"].asString(),
        "Upload lifecycle is temporarily frozen for rollback"
    );
    EXPECT_TRUE((*json)["data"].isNull());
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
