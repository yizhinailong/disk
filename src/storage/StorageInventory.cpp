/**
 * @file StorageInventory.cpp
 * @brief 本地存储对象清单实现
 */

#include "storage/StorageInventory.hpp"

#include <algorithm>
#include <system_error>
#include <utility>

namespace disk::storage {
    namespace {
        [[nodiscard]] auto IsSafeContinuationToken(const std::string& token) -> bool {
            if (token.empty()) {
                return true;
            }
            const auto path = std::filesystem::path(token);
            if (path.is_absolute() || path.lexically_normal().generic_string() != token) {
                return false;
            }
            return std::ranges::none_of(path, [](const auto& component) {
                return component == "..";
            });
        }
    } // namespace

    auto ListLocalStorageInventory(
        const std::filesystem::path& root,
        const std::string& continuation_token,
        size_t limit,
        LocalInventoryLocator locator_mode
    ) -> Result<StorageInventoryPage> {
        if (root.empty() || limit == 0 || limit > kMaxStorageInventoryPageSize ||
            !IsSafeContinuationToken(continuation_token)) {
            return std::unexpected(
                ErrorInfo(ErrorCode::ValidationFailed, "Invalid local storage inventory request")
            );
        }

        std::error_code error;
        if (!std::filesystem::exists(root, error)) {
            if (error) {
                return std::unexpected(
                    ErrorInfo(ErrorCode::InternalError, "Failed to inspect local inventory root")
                );
            }
            return StorageInventoryPage{};
        }
        if (!std::filesystem::is_directory(root, error) || error) {
            return std::unexpected(
                ErrorInfo(ErrorCode::InternalError, "Local inventory root is not a directory")
            );
        }

        std::vector<std::pair<std::string, uint64_t>> entries;
        for (std::filesystem::recursive_directory_iterator iterator(root, error), end;
             iterator != end;
             iterator.increment(error)) {
            if (error) {
                return std::unexpected(
                    ErrorInfo(ErrorCode::InternalError, "Failed to enumerate local storage inventory")
                );
            }
            const auto status = iterator->symlink_status(error);
            if (error) {
                return std::unexpected(
                    ErrorInfo(ErrorCode::InternalError, "Failed to inspect local inventory object")
                );
            }
            if (!std::filesystem::is_regular_file(status)) {
                continue;
            }

            const auto relative = std::filesystem::relative(iterator->path(), root, error);
            if (error || relative.empty() || relative.is_absolute()) {
                return std::unexpected(
                    ErrorInfo(ErrorCode::InternalError, "Failed to normalize local inventory object")
                );
            }
            const auto size = iterator->file_size(error);
            if (error) {
                return std::unexpected(
                    ErrorInfo(ErrorCode::InternalError, "Failed to read local inventory object size")
                );
            }
            entries.emplace_back(relative.generic_string(), static_cast<uint64_t>(size));
        }
        if (error) {
            return std::unexpected(
                ErrorInfo(ErrorCode::InternalError, "Failed to enumerate local storage inventory")
            );
        }

        std::ranges::sort(entries, {}, &std::pair<std::string, uint64_t>::first);
        const auto begin = std::ranges::upper_bound(
            entries,
            continuation_token,
            {},
            &std::pair<std::string, uint64_t>::first
        );
        const auto remaining = static_cast<size_t>(std::distance(begin, entries.end()));
        const auto page_size = std::min(limit, remaining);

        StorageInventoryPage page;
        page.objects.reserve(page_size);
        for (auto iterator = begin; iterator != entries.end() && page.objects.size() < page_size;
             ++iterator) {
            const auto locator = locator_mode == LocalInventoryLocator::IncludeRoot ? (root / iterator->first).generic_string() : iterator->first;
            page.objects.push_back(StorageInventoryObject{
                .locator = locator,
                .size_bytes = iterator->second,
            });
        }
        page.has_more = remaining > page_size;
        if (page.has_more && !page.objects.empty()) {
            page.continuation_token = entries[static_cast<size_t>(std::distance(entries.begin(), begin)) + page_size - 1]
                                          .first;
        }
        return page;
    }

} // namespace disk::storage
