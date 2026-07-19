#include "storage/StorageInventory.hpp"

#include <filesystem>
#include <fstream>
#include <string>

#include <drogon/utils/Utilities.h>
#include <gtest/gtest.h>

namespace disk::storage {
    namespace {
        class TemporaryInventoryRoot {
        public:
            TemporaryInventoryRoot()
                : path(std::filesystem::temp_directory_path() / ("disk-inventory-" + drogon::utils::getUuid())) {
                std::filesystem::create_directories(path / "a");
                std::filesystem::create_directories(path / "b");
            }

            ~TemporaryInventoryRoot() {
                std::error_code error;
                std::filesystem::remove_all(path, error);
            }

            auto Write(const std::filesystem::path& relative, std::string value) const -> void {
                std::ofstream output(path / relative, std::ios::binary);
                output << value;
            }

            std::filesystem::path path;
        };

        TEST(StorageInventoryTest, ListsLocalFilesWithStableBoundedCursor) {
            TemporaryInventoryRoot root;
            root.Write("b/second.bin", "22");
            root.Write("a/first.bin", "1");
            root.Write("a/third.bin", "333");

            auto first = ListLocalStorageInventory(
                root.path,
                {},
                2,
                LocalInventoryLocator::RelativeToRoot
            );
            ASSERT_TRUE(first.has_value());
            ASSERT_EQ(first->objects.size(), 2U);
            EXPECT_EQ(first->objects[0].locator, "a/first.bin");
            EXPECT_EQ(first->objects[0].size_bytes, 1U);
            EXPECT_EQ(first->objects[1].locator, "a/third.bin");
            EXPECT_TRUE(first->has_more);
            EXPECT_EQ(first->continuation_token, "a/third.bin");

            auto second = ListLocalStorageInventory(
                root.path,
                first->continuation_token,
                2,
                LocalInventoryLocator::RelativeToRoot
            );
            ASSERT_TRUE(second.has_value());
            ASSERT_EQ(second->objects.size(), 1U);
            EXPECT_EQ(second->objects[0].locator, "b/second.bin");
            EXPECT_EQ(second->objects[0].size_bytes, 2U);
            EXPECT_FALSE(second->has_more);
            EXPECT_TRUE(second->continuation_token.empty());
        }

        TEST(StorageInventoryTest, RejectsTraversalCursor) {
            TemporaryInventoryRoot root;
            auto result = ListLocalStorageInventory(
                root.path,
                "../outside",
                10,
                LocalInventoryLocator::RelativeToRoot
            );

            ASSERT_FALSE(result.has_value());
            EXPECT_EQ(result.error().code, ErrorCode::ValidationFailed);
        }
    } // namespace
} // namespace disk::storage
