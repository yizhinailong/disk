/**
 * @file ModelDialect_test.cpp
 * @brief Generated ORM dialect regression tests
 *
 * @copyright Copyright (c) 2026
 */

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include <gtest/gtest.h>

namespace {

    auto SourceRoot() -> std::filesystem::path {
        auto current = std::filesystem::current_path();
        for (;;) {
            if (std::filesystem::exists(current / "config.json") &&
                std::filesystem::exists(current / "src" / "models" / "model.json")) {
                return current;
            }

            if (!current.has_parent_path() || current == current.parent_path()) {
                break;
            }
            current = current.parent_path();
        }

        return std::filesystem::current_path();
    }

    auto ReadTextFile(const std::filesystem::path& path) -> std::string {
        std::ifstream input{ path };
        std::ostringstream buffer;
        buffer << input.rdbuf();
        return buffer.str();
    }

} // namespace

TEST(ModelDialectTest, GeneratorConfigTargetsDevelopmentPostgreSql) {
    const auto config = ReadTextFile(SourceRoot() / "src" / "models" / "model.json");

    EXPECT_NE(config.find("\"rdbms\": \"postgresql\""), std::string::npos);
    EXPECT_NE(config.find("\"user\": \"postgres\""), std::string::npos);
    EXPECT_EQ(config.find("mysql"), std::string::npos);
}

TEST(ModelDialectTest, GeneratedInsertUsesPostgreSqlPlaceholdersAndReturning) {
    const auto header = ReadTextFile(SourceRoot() / "src" / "models" / "Folders.hpp");
    const auto source = ReadTextFile(SourceRoot() / "src" / "models" / "Folders.cpp");

    EXPECT_NE(header.find("where id = $1"), std::string::npos);
    EXPECT_NE(header.find("\"$%d,\""), std::string::npos);
    EXPECT_NE(header.find("returning *"), std::string::npos);
    EXPECT_NE(source.find("tableName = \"\\\"folders\\\"\""), std::string::npos);
}

TEST(ModelDialectTest, GeneratedMetadataDoesNotRetainMySqlUnsignedTypes) {
    const auto folder_source = ReadTextFile(SourceRoot() / "src" / "models" / "Folders.cpp");
    const auto file_source = ReadTextFile(SourceRoot() / "src" / "models" / "Files.cpp");

    EXPECT_EQ(folder_source.find("bigint unsigned"), std::string::npos);
    EXPECT_EQ(folder_source.find("int unsigned"), std::string::npos);
    EXPECT_EQ(file_source.find("bigint unsigned"), std::string::npos);
    EXPECT_NE(folder_source.find("timestamp without time zone"), std::string::npos);
}

TEST(ModelDialectTest, ShareQueriesDecodePostgreSqlBooleanExpressionsAsBool) {
    const auto share_source = ReadTextFile(SourceRoot() / "src" / "services" / "ShareService.cpp");

    EXPECT_NE(share_source.find("[\"is_expired\"].as<bool>()"), std::string::npos);
    EXPECT_EQ(share_source.find("[\"is_expired\"].as<int>()"), std::string::npos);
}
