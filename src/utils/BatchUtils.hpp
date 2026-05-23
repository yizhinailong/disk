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
        static auto BuildInPlaceholders(size_t count, size_t start_index = 1) -> std::string {
            if (count == 0) {
                return "";
            }

            std::ostringstream oss;
            oss << "$" << start_index;

            for (size_t i = 1; i < count; ++i) {
                oss << ",$" << (start_index + i);
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
         * @param start_index 第一个占位符的参数索引（默认1）
         * @return std::string 逗号分隔的参数占位符字符串
         */
        template <typename T>
        [[nodiscard]]
        static auto BuildInPlaceholders(const std::vector<T>& items, size_t start_index = 1) -> std::string {
            return BuildInPlaceholders(items.size(), start_index);
        }

        // ==================== 数值 IN 子句 ====================

        /**
         * @brief 构建安全的数字类型 SQL IN 子句
         *
         * 将 uint64_t ID 列表直接嵌入 SQL 语句，生成逗号分隔的数字字面量。
         * 由于输入类型严格为 uint64_t，不存在 SQL 注入风险。
         * 此为所有批处理路径的统一入口，禁止在 Service 层自行拼接。
         *
         * @param ids 要嵌入的 ID 列表（必须为 uint64_t，不接受字符串）
         * @return std::string 逗号分隔的 ID 字符串，如 "1,2,3,4"；空列表返回空串
         *
         * @code
         * std::vector<uint64_t> file_ids = {1, 2, 3};
         * auto in_clause = BatchUtils::BuildSafeNumericInClause(file_ids);
         * // in_clause = "1,2,3"
         * auto sql = "SELECT * FROM files WHERE id IN (" + in_clause + ")";
         * @endcode
         */
        [[nodiscard]]
        static auto BuildSafeNumericInClause(const std::vector<uint64_t>& ids) -> std::string {
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
    };

} // namespace disk::utils
