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
    EXPECT_TRUE((*json)["data"].isNull()) << "Data should be null for void result";
}

TEST(Response, FromResultVoidError) {
    Result<void> result = std::unexpected(ErrorInfo(ErrorCode::UsernameExists));

    auto response = Response::FromResult(result);

    ASSERT_NE(response, nullptr) << "Response should not be null";

    auto json = response->getJsonObject();
    ASSERT_NE(json, nullptr) << "JSON object should not be null";

    EXPECT_EQ((*json)["code"].asInt(), static_cast<int>(ErrorCode::UsernameExists))
        << "Code should match error code";
    EXPECT_FALSE((*json)["message"].asString().empty())
        << "Message should not be empty";

    EXPECT_TRUE((*json)["data"].isNull()) << "Data should be null for error response";
}
