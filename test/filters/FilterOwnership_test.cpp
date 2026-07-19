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
        std::ifstream input{ path };
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
        return ReadTextFile(SourceRoot() / "src" / "controllers" / std::string{ controller_name });
    }

} // namespace

TEST(FilterOwnershipTest, GlobalFiltersContainOnlyGlobalOwners) {
    const auto config_text = ReadTextFile(SourceRoot() / "config.json");

    EXPECT_EQ(CountOccurrences(config_text, "\"name\": \"drogon::plugin::GlobalFilters\""), 1U);
    EXPECT_TRUE(ContainsAllInOrder(
        config_text,
        {
            "disk::filters::RequestTraceFilter",
            "disk::filters::JwtAuthFilter",
            "disk::filters::RegisterRateLimitFilter",
        }
    ));

    const std::unordered_set<std::string_view> global_filters{
        "disk::filters::RequestTraceFilter",
        "disk::filters::JwtAuthFilter",
        "disk::filters::RegisterRateLimitFilter",
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
        "disk::filters::ShareAccessRateLimitFilter",
        "disk::filters::ShareOperationRateLimitFilter",
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
    EXPECT_NE(jwt_filter.find("path == \"/api/health/live\""), std::string::npos);
    EXPECT_NE(jwt_filter.find("path == \"/api/health/ready\""), std::string::npos);
    EXPECT_NE(jwt_filter.find("path == \"/metrics\""), std::string::npos);
    EXPECT_NE(jwt_filter.find("/api/share/access/"), std::string::npos);
    EXPECT_NE(jwt_filter.find("/api/share/browse/"), std::string::npos);
    EXPECT_NE(jwt_filter.find("/api/share/download/"), std::string::npos);
    EXPECT_EQ(jwt_filter.find("/api/share/save/"), std::string::npos);

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

TEST(FilterOwnershipTest, ShareRateLimitersHaveExactRouteOwnershipAndOrder) {
    const auto share_controller = ControllerText("ShareController.hpp");

    EXPECT_EQ(
        CountOccurrences(share_controller, "\"disk::filters::ShareAccessRateLimitFilter\""),
        1U
    );
    EXPECT_EQ(
        CountOccurrences(share_controller, "\"disk::filters::ShareAuthFilter\""),
        4U
    );
    EXPECT_EQ(
        CountOccurrences(share_controller, "\"disk::filters::ShareOperationRateLimitFilter\""),
        4U
    );
    EXPECT_TRUE(ContainsAllInOrder(
        share_controller,
        {
            "ShareController::Access",
            "\"disk::filters::ShareAccessRateLimitFilter\"",
            "ShareController::Browse",
            "\"disk::filters::ShareAuthFilter\"",
            "\"disk::filters::ShareOperationRateLimitFilter\"",
            "ShareController::DownloadInfo",
            "\"disk::filters::ShareAuthFilter\"",
            "\"disk::filters::ShareOperationRateLimitFilter\"",
            "ShareController::Download",
            "\"disk::filters::ShareAuthFilter\"",
            "\"disk::filters::ShareOperationRateLimitFilter\"",
            "ShareController::Save",
            "\"disk::filters::ShareAuthFilter\"",
            "\"disk::filters::ShareOperationRateLimitFilter\"",
        }
    ));
}

TEST(FilterOwnershipTest, RateLimitFiltersFailOpenOnRedisFailure) {
    const std::vector<std::string_view> filter_files{
        "UploadRateLimitFilter.cpp",
        "DownloadRateLimitFilter.cpp",
        "FolderRateLimitFilter.cpp",
        "AdminRateLimitFilter.cpp",
        "RegisterRateLimitFilter.cpp",
    };

    for (const auto filter_file : filter_files) {
        const auto text = ReadTextFile(SourceRoot() / "src" / "filters" / std::string{ filter_file });
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
        {   "UploadRateLimitFilter.cpp",{ "GetUploadRateLimitWindowSeconds", "GetUploadRateLimitPerMinute" }                                        },
        { "DownloadRateLimitFilter.cpp", { "GetDownloadRateLimitWindowSeconds", "GetDownloadRateLimitPerMinute" } },
        {   "FolderRateLimitFilter.cpp",     { "GetFolderRateLimitWindowSeconds", "GetFolderRateLimitPerMinute" } },
        {    "AdminRateLimitFilter.cpp",       { "GetAdminRateLimitWindowSeconds", "GetAdminRateLimitPerMinute" } },
        {    "ShareRateLimitFilter.cpp",
         {
         "GetShareAccessRateLimitWindowSeconds",
         "GetShareAccessRateLimitPerMinute",
         "GetShareBrowseRateLimitWindowSeconds",
         "GetShareBrowseRateLimitPerMinute",
         "GetShareDownloadRateLimitWindowSeconds",
         "GetShareDownloadRateLimitPerMinute",
         }                                                                                                       },
        { "RegisterRateLimitFilter.cpp", { "GetRegisterRateLimitWindowSeconds", "GetRegisterRateLimitPerWindow" } },
    };

    for (const auto& [filter_file, getters] : expected_getters) {
        const auto text = ReadTextFile(SourceRoot() / "src" / "filters" / std::string{ filter_file });
        for (const auto getter : getters) {
            EXPECT_NE(text.find(getter), std::string::npos) << filter_file << ": " << getter;
        }
        EXPECT_NE(text.find("window_seconds"), std::string::npos) << filter_file;
        EXPECT_NE(text.find("const auto limit"), std::string::npos) << filter_file;
    }
}

TEST(FilterOwnershipTest, ObsoleteSharePublicLimiterIsAbsent) {
    const auto root = SourceRoot();
    EXPECT_FALSE(std::filesystem::exists(root / "src" / "filters" / "SharePublicRateLimitFilter.hpp"));
    EXPECT_FALSE(std::filesystem::exists(root / "src" / "filters" / "SharePublicRateLimitFilter.cpp"));
    EXPECT_FALSE(std::filesystem::exists(root / "test" / "filters" / "SharePublicRateLimit_test.cpp"));

    const auto config_text = ReadTextFile(root / "config.json");
    const auto source_cmake = ReadTextFile(root / "src" / "CMakeLists.txt");
    const auto test_cmake = ReadTextFile(root / "test" / "CMakeLists.txt");
    EXPECT_EQ(config_text.find("SharePublicRateLimitFilter"), std::string::npos);
    EXPECT_EQ(source_cmake.find("SharePublicRateLimitFilter"), std::string::npos);
    EXPECT_EQ(test_cmake.find("SharePublicRateLimit"), std::string::npos);
}

TEST(FilterOwnershipTest, ShareOperationLimiterDoesNotReadReplayableHeaders) {
    const auto filter = ReadTextFile(
        SourceRoot() / "src" / "filters" / "ShareRateLimitFilter.cpp"
    );

    EXPECT_EQ(filter.find("X-Share-Token"), std::string::npos);
    EXPECT_EQ(filter.find("Authorization"), std::string::npos);
    EXPECT_EQ(filter.find("getHeader"), std::string::npos);
    EXPECT_NE(filter.find("SHARE_TOKEN_JTI_ATTRIBUTE"), std::string::npos);
    EXPECT_NE(filter.find("BuildShareAccessRateLimitKey"), std::string::npos);
    EXPECT_NE(filter.find("BuildShareBrowseRateLimitKey"), std::string::npos);
    EXPECT_NE(filter.find("BuildShareDownloadRateLimitKey"), std::string::npos);
}
