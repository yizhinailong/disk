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

TEST(FilterOwnershipTest, GlobalFiltersContainGlobalJwtAndPublicRateLimiters) {
    const auto config_text = ReadTextFile(SourceRoot() / "config.json");

    EXPECT_EQ(CountOccurrences(config_text, "\"name\": \"drogon::plugin::GlobalFilters\""), 1U);
    EXPECT_TRUE(ContainsAllInOrder(
        config_text,
        {
            "disk::filters::RequestTraceFilter",
            "disk::filters::JwtAuthFilter",
            "disk::filters::RegisterRateLimitFilter",
            "disk::filters::SharePublicRateLimitFilter",
        }
    ));

    const std::unordered_set<std::string_view> global_filters{
        "disk::filters::RequestTraceFilter",
        "disk::filters::JwtAuthFilter",
        "disk::filters::RegisterRateLimitFilter",
        "disk::filters::SharePublicRateLimitFilter",
    };

    for (const auto filter : global_filters) {
        EXPECT_EQ(CountOccurrences(config_text, filter), 1U) << filter;
    }

    const std::unordered_set<std::string_view> route_owned_filters{
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

TEST(FilterOwnershipTest, PublicRoutesAreCentralJwtExemptions) {
    const auto jwt_filter = ReadTextFile(SourceRoot() / "src" / "filters" / "JwtAuthFilter.hpp");

    EXPECT_NE(jwt_filter.find("path == \"/api/auth/register\""), std::string::npos);
    EXPECT_NE(jwt_filter.find("path == \"/api/auth/login\""), std::string::npos);
    EXPECT_NE(jwt_filter.find("path == \"/api/auth/refresh\""), std::string::npos);
    EXPECT_NE(jwt_filter.find("path == \"/api/health\""), std::string::npos);
    EXPECT_NE(jwt_filter.find("/api/share/access/"), std::string::npos);
    EXPECT_NE(jwt_filter.find("/api/share/browse/"), std::string::npos);
    EXPECT_NE(jwt_filter.find("/api/share/download/"), std::string::npos);

    EXPECT_EQ(jwt_filter.find("/api/auth/logout"), std::string::npos);
    EXPECT_EQ(jwt_filter.find("/api/file/list"), std::string::npos);
    EXPECT_EQ(jwt_filter.find("/api/admin/users"), std::string::npos);
}

TEST(FilterOwnershipTest, RouteDeclarationsDoNotDeclareJwtAuthFilter) {
    const std::vector<std::string_view> controllers{
        "AdminController.hpp",
        "AuthController.hpp",
        "FileController.hpp",
        "FolderController.hpp",
        "OperationLogController.hpp",
        "ShareController.hpp",
        "SystemController.hpp",
        "TrashController.hpp",
        "UserController.hpp",
    };

    for (const auto controller : controllers) {
        EXPECT_EQ(CountOccurrences(ControllerText(controller), "disk::filters::JwtAuthFilter"), 0U)
            << controller;
    }
}

TEST(FilterOwnershipTest, AuthenticatedRateLimitersRemainRouteOwnedExactlyOnce) {
    const auto file_controller = ControllerText("FileController.hpp");
    EXPECT_EQ(CountOccurrences(file_controller, "\"disk::filters::UploadRateLimitFilter\""), 4U);
    EXPECT_EQ(CountOccurrences(file_controller, "\"disk::filters::DownloadRateLimitFilter\""), 2U);

    const auto folder_controller = ControllerText("FolderController.hpp");
    EXPECT_EQ(CountOccurrences(folder_controller, "\"disk::filters::FolderRateLimitFilter\""), 4U);

    const auto admin_controller = ControllerText("AdminController.hpp");
    EXPECT_EQ(CountOccurrences(admin_controller, "\"disk::filters::AdminRateLimitFilter\""), 14U);

    EXPECT_TRUE(ContainsAllInOrder(
        admin_controller,
        {
            "AdminController::ListUsers",
            "\"disk::filters::AdminAuthFilter\"",
            "\"disk::filters::AdminRateLimitFilter\"",
        }
    ));
}

TEST(FilterOwnershipTest, RateLimitFiltersFailOpenOnRedisFailure) {
    const std::vector<std::string_view> filter_files{
        "UploadRateLimitFilter.cpp",
        "DownloadRateLimitFilter.cpp",
        "FolderRateLimitFilter.cpp",
        "AdminRateLimitFilter.cpp",
        "SharePublicRateLimitFilter.cpp",
        "RegisterRateLimitFilter.cpp",
    };

    for (const auto filter_file : filter_files) {
        const auto text = ReadTextFile(SourceRoot() / "src" / "filters" / std::string{filter_file});
        EXPECT_TRUE(ContainsAllInOrder(
            text,
            {
                "auto incr_result = co_await CheckFixedWindowLimit",
                "if (!incr_result)",
                "Logger::Error() << \"Redis IncrWithExpire failed:",
                "co_return nullptr;",
            }
        )) << filter_file;
    }
}

TEST(FilterOwnershipTest, RateLimitFiltersUseNormalizedConfiguration) {
    const std::vector<std::pair<std::string_view, std::vector<std::string_view>>> expected_getters{
        {"UploadRateLimitFilter.cpp", {"GetUploadRateLimitWindowSeconds", "GetUploadRateLimitPerMinute"}},
        {"DownloadRateLimitFilter.cpp", {"GetDownloadRateLimitWindowSeconds", "GetDownloadRateLimitPerMinute"}},
        {"FolderRateLimitFilter.cpp", {"GetFolderRateLimitWindowSeconds", "GetFolderRateLimitPerMinute"}},
        {"AdminRateLimitFilter.cpp", {"GetAdminRateLimitWindowSeconds", "GetAdminRateLimitPerMinute"}},
        {"SharePublicRateLimitFilter.cpp", {"GetSharePublicRateLimitWindowSeconds", "GetSharePublicRateLimitPerMinute"}},
        {"RegisterRateLimitFilter.cpp", {"GetRegisterRateLimitWindowSeconds", "GetRegisterRateLimitPerWindow"}},
    };

    for (const auto& [filter_file, getters] : expected_getters) {
        const auto text = ReadTextFile(SourceRoot() / "src" / "filters" / std::string{filter_file});
        for (const auto getter : getters) {
            EXPECT_NE(text.find(getter), std::string::npos) << filter_file << ": " << getter;
        }
        EXPECT_NE(text.find("window_seconds"), std::string::npos) << filter_file;
        EXPECT_NE(text.find("const auto limit"), std::string::npos) << filter_file;
    }
}
