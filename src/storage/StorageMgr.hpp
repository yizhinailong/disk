/**
 * @file StorageMgr.hpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 文件存储管理器（单例）
 *
 * @copyright Copyright (c) 2026
 *
 */

#pragma once

#include <memory>

#include "storage/IBlobStorage.hpp"
#include "storage/IFileStorage.hpp"
#include "storage/IUploadStagingStorage.hpp"

namespace disk::storage {

    /**
     * @brief 文件存储管理器（单例）
     *
     * 职责：
     * - 管理上传暂存存储和最终 Blob 存储实例的生命周期
     * - 提供全局访问点
     */
    class StorageMgr {
    public:
        /**
         * @brief 设置拆分后的存储实例（在应用启动时调用）
         * @param staging_storage 上传暂存存储实例
         * @param blob_storage 最终 Blob 存储实例
         */
        static void SetInstances(
            std::shared_ptr<IUploadStagingStorage> staging_storage,
            std::shared_ptr<IBlobStorage> blob_storage
        );

        /**
         * @brief 设置兼容聚合存储实例（在应用启动时调用）
         * @param storage 同时实现上传暂存与最终 Blob 存储的实例
         */
        static void SetInstance(std::shared_ptr<IFileStorage> storage);

        /**
         * @brief 获取上传暂存存储实例
         * @return IUploadStagingStorage* 上传暂存存储实例指针
         */
        [[nodiscard]]
        static auto GetStagingStorage() -> IUploadStagingStorage*;

        /**
         * @brief 获取最终 Blob 存储实例
         * @return IBlobStorage* 最终 Blob 存储实例指针
         */
        [[nodiscard]]
        static auto GetBlobStorage() -> IBlobStorage*;

        /**
         * @brief 获取兼容聚合存储实例
         * @return IFileStorage* 存储实例指针
         */
        [[nodiscard]]
        static auto GetStorage() -> IFileStorage*;

        /**
         * @brief 检查存储实例是否已初始化
         * @return bool 是否已初始化
         */
        [[nodiscard]]
        static auto IsInitialized() -> bool;

    private:
        static std::shared_ptr<IFileStorage> s_storage;
        static std::shared_ptr<IUploadStagingStorage> s_staging_storage;
        static std::shared_ptr<IBlobStorage> s_blob_storage;
    };

} ///< namespace disk::storage
