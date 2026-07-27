/**
 * @file BlobStoreMgr.hpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 最终内容 Blob 存储管理器（单例）
 *
 * @copyright Copyright (c) 2026
 *
 */

#pragma once

#include <memory>

#include "storage/IBlobStore.hpp"

namespace disk::storage {

    /**
     * @brief 最终内容 Blob 存储管理器（单例）
     *
     * 职责：
     * - 管理最终内容 Blob 存储实例的生命周期
     * - 提供全局访问点
     */
    class BlobStoreMgr {
    public:
        /**
         * @brief 设置 Blob 存储实例（在应用启动时调用）
         * @param blob_store Blob 存储实例
         */
        static void SetInstance(std::shared_ptr<IBlobStore> blob_store);

        /**
         * @brief 获取 Blob 存储实例
         * @return IBlobStore* Blob 存储实例指针
         */
        [[nodiscard]]
        static auto GetBlobStore() -> IBlobStore*;

    private:
        static std::shared_ptr<IBlobStore> s_blob_store;
    };

} // namespace disk::storage
