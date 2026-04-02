/**
 * @file BatchUtils.hpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 批量操作工具类（分块和批量操作辅助函数）
 *
 * @copyright Copyright (c) 2026
 *
 */

#pragma once

#include <concepts>
#include <ranges>
#include <sstream>
#include <string>
#include <vector>

namespace disk::utils {

    /**
     * @brief 默认批处理分块大小
     *
     * 用于大型批量操作的优化分块大小（如文件删除、分享取消、回收站清理等）
     */
    constexpr size_t DEFAULT_BATCH_CHUNK_SIZE = 500;

    /**
     * @brief 分块范围视图
     *
     * 表示容器的一个连续分片，用于批量操作的分块处理。
     * 支持范围遍历，适用于 std::span、迭代器对等范围类型。
     *
     * @tparam RangeType 范围类型（如 std::span<T>、std::vector<T>）
     */
    template <typename RangeType>
    struct ChunkRange {
        RangeType range;

        /// 迭代器类型
        using iterator = decltype(std::begin(range));
        using const_iterator = decltype(std::cbegin(range));
        using value_type = typename std::iterator_traits<iterator>::value_type;

        /// 获取分块大小
        [[nodiscard]]
        auto Size() const noexcept -> size_t {
            return std::ranges::size(range);
        }

        /// 起始迭代器
        [[nodiscard]]
        auto begin() const -> iterator {
            return std::begin(range);
        }

        /// 结束迭代器
        [[nodiscard]]
        auto end() const -> iterator {
            return std::end(range);
        }

        /// 常量起始迭代器
        [[nodiscard]]
        auto CBegin() const -> const_iterator {
            return std::cbegin(range);
        }

        /// 常量结束迭代器
        [[nodiscard]]
        auto CEnd() const -> const_iterator {
            return std::cend(range);
        }

        /// 访问指定索引的元素
        [[nodiscard]]
        auto At(size_t index) const -> const value_type& {
            return range[index];
        }
    };

    /**
     * @brief 批量操作工具类
     *
     * 提供批量操作的辅助功能：
     * - 分块：将大型向量分割为固定大小的分块
     * - IN-clause 占位符：为 SQL 参数化查询生成 "? 占位符" 字符串
     */
    class BatchUtils {
    public:
        // ==================== 分块操作 ====================

        /**
         * @brief 将向量分割为多个分块
         *
         * 将大型向量分割为指定大小的分块，每个分块可以独立处理。
         * 适用于批量操作（如批量删除、批量更新等），避免单次操作数据量过大。
         *
         * @tparam T 元素类型（通常是 uint64_t 或 std::string）
         * @param items 要分割的向量
         * @param chunk_size 每个分块的大小（默认使用 DEFAULT_BATCH_CHUNK_SIZE）
         * @return std::vector<std::vector<T>> 分块后的向量集合
         *
         * @code
         * std::vector<uint64_t> file_ids = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
         * auto chunks = BatchUtils::Chunk(file_ids, 3);
         * // chunks = {{1, 2, 3}, {4, 5, 6}, {7, 8, 9}, {10}}
         * @endcode
         */
        template <typename T>
        [[nodiscard]]
        static auto Chunk(const std::vector<T>& items, size_t chunk_size = DEFAULT_BATCH_CHUNK_SIZE)
            -> std::vector<std::vector<T>> {
            std::vector<std::vector<T>> chunks;
            chunks.reserve((items.size() + chunk_size - 1) / chunk_size);

            for (size_t i = 0; i < items.size(); i += chunk_size) {
                auto last = std::min(i + chunk_size, items.size());
                chunks.emplace_back(items.begin() + i, items.begin() + last);
            }

            return chunks;
        }

        /**
         * @brief 创建分块范围视图（零拷贝）
         *
         * 返回分块范围的视图，不复制数据。
         * 适用于只读遍历和批量处理场景。
         *
         * @tparam T 元素类型
         * @param items 要分块的向量
         * @param chunk_size 每个分块的大小
         * @return std::vector<ChunkRange<std::span<const T>>> 分块视图集合
         *
         * @code
         * std::vector<uint64_t> file_ids = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
         * auto chunk_views = BatchUtils::ChunkViews(file_ids, 3);
         * for (const auto& chunk : chunk_views) {
         *     for (const auto& id : chunk) {
         *         // 处理每个 ID
         *     }
         * }
         * @endcode
         */
        template <typename T>
        [[nodiscard]]
        static auto ChunkViews(const std::vector<T>& items, size_t chunk_size = DEFAULT_BATCH_CHUNK_SIZE)
            -> std::vector<ChunkRange<std::span<const T>>> {
            std::vector<ChunkRange<std::span<const T>>> chunk_views;
            chunk_views.reserve((items.size() + chunk_size - 1) / chunk_size);

            for (size_t i = 0; i < items.size(); i += chunk_size) {
                auto last = std::min(i + chunk_size, items.size());
                chunk_views.emplace_back(std::span<const T>{ items.data() + i, items.data() + last });
            }

            return chunk_views;
        }

        // ==================== SQL 占位符生成 ====================

        /**
         * @brief 构建 SQL IN-clause 占位符字符串
         *
         * 生成用于 Drogon execSqlCoro 参数化查询的占位符字符串。
         * 例如：count=4 返回 "?,?,?,?"
         *
         * @param count 占位符数量
         * @return std::string 逗号分隔的问号占位符字符串
         *
         * @code
         * std::vector<uint64_t> file_ids = {1, 2, 3, 4};
         * auto placeholders = BatchUtils::BuildInPlaceholders(file_ids.size());
         * // placeholders = "?,?,?,?"
         *
         * // 使用示例
         * auto placeholders = BatchUtils::BuildInPlaceholders(file_ids.size());
         * auto sql = "DELETE FROM files WHERE file_id IN (" + placeholders + ")";
         * auto result = co_await client->execSqlCoro(sql, file_ids);
         * @endcode
         */
        [[nodiscard]]
        static auto BuildInPlaceholders(size_t count) -> std::string {
            if (count == 0) {
                return "";
            }

            std::ostringstream oss;
            oss << "?";

            for (size_t i = 1; i < count; ++i) {
                oss << ",?";
            }

            return oss.str();
        }

        /**
         * @brief 构建 SQL IN-clause 占位符字符串（从向量）
         *
         * 便捷重载，直接从向量大小生成占位符。
         *
         * @tparam T 元素类型（仅使用容器大小，忽略元素值）
         * @param items 向量容器
         * @return std::string 逗号分隔的问号占位符字符串
         */
        template <typename T>
        [[nodiscard]]
        static auto BuildInPlaceholders(const std::vector<T>& items) -> std::string {
            return BuildInPlaceholders(items.size());
        }

        // ==================== 数值 IN 子句 ====================

        /**
         * @brief 构建数字类型的 SQL IN 子句（字面量，不使用参数绑定）
         *
         * 用于安全的 uint64_t ID 列表，直接嵌入 SQL 语句。
         * 适用于不需要参数绑定的整数 ID 场景。
         *
         * @param ids 要嵌入的 ID 列表
         * @return std::string 逗号分隔的 ID 字符串，如 "1,2,3,4"
         *
         * @code
         * std::vector<uint64_t> file_ids = {1, 2, 3};
         * auto in_clause = BatchUtils::BuildNumericInClause(file_ids);
         * // in_clause = "1,2,3"
         * auto sql = "SELECT * FROM files WHERE id IN (" + in_clause + ")";
         * @endcode
         */
        [[nodiscard]]
        static auto BuildNumericInClause(const std::vector<uint64_t>& ids) -> std::string {
            if (ids.empty()) {
                return "";
            }
            std::ostringstream oss;
            for (size_t i = 0; i < ids.size(); ++i) {
                if (i > 0) {
                    oss << ",";
                }
                oss << ids[i];
            }
            return oss.str();
        }

        // ==================== 批量验证 ====================

        /**
         * @brief 验证批量操作的输入参数
         *
         * 检查向量是否为空或超过合理的批处理限制。
         *
         * @tparam T 元素类型
         * @param items 要验证的向量
         * @param max_limit 最大允许的批处理数量（默认 10000）
         * @return bool 是否有效（非空且未超过限制）
         */
        template <typename T>
        [[nodiscard]]
        static auto ValidateBatchInput(const std::vector<T>& items, size_t max_limit = 10000) -> bool {
            return !items.empty() && items.size() <= max_limit;
        }

        /**
         * @brief 计算需要的分块数量
         *
         * @param total_items 总项目数
         * @param chunk_size 每个分块的大小
         * @return size_t 分块数量
         */
        [[nodiscard]]
        static auto CalculateChunkCount(size_t total_items, size_t chunk_size) noexcept -> size_t {
            if (chunk_size == 0) {
                return 0;
            }
            return (total_items + chunk_size - 1) / chunk_size;
        }
    };

} // namespace disk::utils
