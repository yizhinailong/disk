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

#include "storage/UploadStagingStorage.hpp"

namespace disk::storage {

    /**
     * @brief 文件存储管理器（单例）
     *
     * 职责：
     * - 管理文件存储实例的生命周期
     * - 提供全局访问点
     *
     * 使用方式：
     * @code
     * ///< 在 main.cpp 初始化
     * StorageMgr::SetInstance(std::make_shared<LocalFileStorage>(config));
     *
     * ///< 在控制器中获取
     * auto* storage = StorageMgr::GetUploadStagingStorage();
     * @endcode
     */
    class StorageMgr {
    public:
        /**
         * @brief 设置存储实例（在应用启动时调用）
         * @param storage 存储实例
         */
        static void SetInstance(std::shared_ptr<UploadStagingStorage> storage);

        /**
         * @brief 获取上传暂存存储实例
         * @return UploadStagingStorage* 上传暂存存储实例指针
         */
        [[nodiscard]]
        static auto GetUploadStagingStorage() -> UploadStagingStorage*;

    private:
        static std::shared_ptr<UploadStagingStorage> s_upload_staging_storage;
    };

} // namespace disk::storage
