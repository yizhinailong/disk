/**
 * @file FilterOwnership_test.cpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 请求过滤器归属配置测试
 *
 * @copyright Copyright (c) 2026
 *
 */

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

#include <gtest/gtest.h>

namespace {

    auto ReadTextFile(const std::filesystem::path& path) -> std::string {
        std::ifstream input{path};
        std::ostringstream buffer;
        buffer << input.rdbuf();
        return buffer.str();
    }

    auto SourceRoot() -> std::filesystem::path {
        auto current = std::filesystem::current_path();
        for (;;) {
            if (std::filesystem::exists(current / "config.json") &&
                std::filesystem::exists(current / "src" / "controllers")) {
                return current;
            }

            if (!current.has_parent_path() || current == current.parent_path()) {
                break;
            }
            current = current.parent_path();
        }

        return std::filesystem::current_path();
    }

    auto CountOccurrences(std::string_view text, std::string_view needle) -> size_t {
        size_t count{};
        size_t pos{};
        while ((pos = text.find(needle, pos)) != std::string_view::npos) {
            ++count;
            pos += needle.size();
        }
        return count;
    }

    auto ContainsAllInOrder(std::string_view text, const std::vector<std::string_view>& needles) -> bool {
        size_t pos{};
        for (const auto needle : needles) {
            pos = text.find(needle, pos);
            if (pos == std::string_view::npos) {
                return false;
            }
            pos += needle.size();
        }
        return true;
    }

    auto ControllerText(std::string_view controller_name) -> std::string {
        return ReadTextFile(SourceRoot() / "src" / "controllers" / std::string{controller_name});
    }

} // namespace

TEST(FilterOwnershipTest, GlobalFiltersOnlyContainGlobalSafeFilters) {
    const auto config_text = ReadTextFile(SourceRoot() / "config.json");

    const std::unordered_set<std::string_view> global_filters{
        "disk::filters::RequestTraceFilter",
        "disk::filters::RegisterRateLimitFilter",
        "disk::filters::SharePublicRateLimitFilter",
    };

    for (const auto filter : global_filters) {
        EXPECT_EQ(CountOccurrences(config_text, filter), 1U) << filter;
    }

    const std::unordered_set<std::string_view> route_owned_filters{
        "disk::filters::JwtAuthFilter",
        "disk::filters::AdminAuthFilter",
        "disk::filters::DownloadRateLimitFilter",
        "disk::filters::AdminRateLimitFilter",
        "disk::filters::FolderRateLimitFilter",
        "disk::filters::UploadRateLimitFilter",
        "disk::filters::RateLimitFilter",
        "disk::filters::ShareAuthFilter",
    };

    for (const auto filter : route_owned_filters) {
        EXPECT_EQ(CountOccurrences(config_text, filter), 0U) << filter;
    }
}

TEST(FilterOwnershipTest, PublicRoutesKeepBearerJwtOutOfRouteDeclarations) {
    const auto auth_controller = ControllerText("AuthController.hpp");
    EXPECT_TRUE(ContainsAllInOrder(
        auth_controller,
        {
            "AuthController::Register",
            "\"/api/auth/register\"",
            "drogon::Post",
        }
    ));
    EXPECT_TRUE(ContainsAllInOrder(
        auth_controller,
        {
            "AuthController::Login",
            "\"/api/auth/login\"",
            "drogon::Post",
        }
    ));
    EXPECT_TRUE(ContainsAllInOrder(
        auth_controller,
        {
            "AuthController::RefreshTokens",
            "\"/api/auth/refresh\"",
            "drogon::Post",
        }
    ));
    EXPECT_TRUE(ContainsAllInOrder(
        auth_controller,
        {
            "AuthController::RefreshTokens",
            "ADD_METHOD_TO(\n            AuthController::Logout",
        }
    ));

    const auto health_controller = ControllerText("HealthController.hpp");
    EXPECT_TRUE(ContainsAllInOrder(
        health_controller,
        {
            "HealthController::Check",
            "\"/api/health\"",
            "drogon::Get",
        }
    ));
    EXPECT_EQ(CountOccurrences(health_controller, "JwtAuthFilter"), 0U);

    const auto share_controller = ControllerText("ShareController.hpp");
    EXPECT_TRUE(ContainsAllInOrder(
        share_controller,
        {
            "ShareController::Access",
            "\"/api/share/access/{share_id}\"",
            "drogon::Post",
        }
    ));
    EXPECT_TRUE(ContainsAllInOrder(
        share_controller,
        {
            "ShareController::Access",
            "ADD_METHOD_TO(\n            ShareController::Browse",
            "\"disk::filters::ShareAuthFilter\"",
        }
    ));
}

TEST(FilterOwnershipTest, LogoutRouteDeclaresJwtAuthFilter) {
    const auto auth_controller = ControllerText("AuthController.hpp");

    EXPECT_TRUE(ContainsAllInOrder(
        auth_controller,
        {
            "AuthController::Logout",
            "\"/api/auth/logout\"",
            "drogon::Post",
            "\"disk::filters::JwtAuthFilter\"",
        }
    ));
}

TEST(FilterOwnershipTest, AuthenticatedRateLimitersRunAfterJwtAuthFilter) {
    const auto file_controller = ControllerText("FileController.hpp");
    EXPECT_TRUE(ContainsAllInOrder(
        file_controller,
        {
            "FileController::DownloadInfo",
            "\"disk::filters::JwtAuthFilter\"",
            "\"disk::filters::DownloadRateLimitFilter\"",
        }
    ));
    EXPECT_TRUE(ContainsAllInOrder(
        file_controller,
        {
            "FileController::Download",
            "\"disk::filters::JwtAuthFilter\"",
            "\"disk::filters::DownloadRateLimitFilter\"",
        }
    ));

    const auto folder_controller = ControllerText("FolderController.hpp");
    EXPECT_EQ(CountOccurrences(folder_controller, "\"disk::filters::FolderRateLimitFilter\""), 4U);
    EXPECT_EQ(CountOccurrences(folder_controller, "\"disk::filters::JwtAuthFilter\","), 4U);
    EXPECT_TRUE(ContainsAllInOrder(
        folder_controller,
        {
            "FolderController::CreateFolder",
            "\"disk::filters::JwtAuthFilter\"",
            "\"disk::filters::FolderRateLimitFilter\"",
        }
    ));

    const auto admin_controller = ControllerText("AdminController.hpp");
    EXPECT_EQ(CountOccurrences(admin_controller, "\"disk::filters::AdminRateLimitFilter\""), 13U);
    EXPECT_TRUE(ContainsAllInOrder(
        admin_controller,
        {
            "AdminController::ListUsers",
            "\"disk::filters::JwtAuthFilter\"",
            "\"disk::filters::AdminAuthFilter\"",
            "\"disk::filters::AdminRateLimitFilter\"",
        }
    ));
}
