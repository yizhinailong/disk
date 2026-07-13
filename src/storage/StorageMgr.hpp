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

#include "storage/IFileStorage.hpp"

namespace disk::storage {

    /**
     * @brief 文件存储管理器（单例）
     *
     * 职责：
     * - 管理上传暂存存储与最终 Blob 存储实例的生命周期
     * - 提供按职责拆分的全局访问点
     */
    class StorageMgr {
    public:
        /**
         * @brief 设置兼容的完整本地存储实例（在应用启动时调用）
         * @param storage 存储实例，同时承担暂存与 Blob 角色
         */
        static void SetInstance(std::shared_ptr<IFileStorage> storage);

        /**
         * @brief 设置拆分后的存储实例
         * @param upload_staging_storage 上传暂存存储实例
         * @param blob_store 最终 Blob 存储实例
         */
        static void SetInstance(
            std::shared_ptr<UploadStagingStorage> upload_staging_storage,
            std::shared_ptr<BlobStore> blob_store
        );

        /**
         * @brief 获取上传暂存存储实例
         * @return UploadStagingStorage* 上传暂存存储实例指针
         */
        [[nodiscard]]
        static auto GetUploadStagingStorage() -> UploadStagingStorage*;

        /**
         * @brief 获取最终 Blob 存储实例
         * @return BlobStore* 最终 Blob 存储实例指针
         */
        [[nodiscard]]
        static auto GetBlobStore() -> BlobStore*;

        /**
         * @brief 获取兼容完整存储实例
         * @return IFileStorage* 存储实例指针
         * @note 新代码应优先使用 GetUploadStagingStorage() 或 GetBlobStore()。
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
        static std::shared_ptr<UploadStagingStorage> s_upload_staging_storage;
        static std::shared_ptr<BlobStore> s_blob_store;
    };

} ///< namespace disk::storage
