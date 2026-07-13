/**
 * @file StorageMgr.cpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 文件存储管理器实现
 *
 * @copyright Copyright (c) 2026
 *
 */

#include "StorageMgr.hpp"

namespace disk::storage {

    std::shared_ptr<IFileStorage> StorageMgr::s_storage = nullptr;
    std::shared_ptr<IUploadStagingStorage> StorageMgr::s_staging_storage = nullptr;
    std::shared_ptr<IBlobStorage> StorageMgr::s_blob_storage = nullptr;

    void StorageMgr::SetInstances(
        std::shared_ptr<IUploadStagingStorage> staging_storage,
        std::shared_ptr<IBlobStorage> blob_storage
    ) {
        s_storage = nullptr;
        s_staging_storage = std::move(staging_storage);
        s_blob_storage = std::move(blob_storage);
    }

    void StorageMgr::SetInstance(std::shared_ptr<IFileStorage> storage) {
        s_storage = std::move(storage);
        s_staging_storage = s_storage;
        s_blob_storage = s_storage;
    }

    auto StorageMgr::GetStagingStorage() -> IUploadStagingStorage* {
        return s_staging_storage.get();
    }

    auto StorageMgr::GetBlobStorage() -> IBlobStorage* {
        return s_blob_storage.get();
    }

    auto StorageMgr::GetStorage() -> IFileStorage* {
        return s_storage.get();
    }

    auto StorageMgr::IsInitialized() -> bool {
        return s_staging_storage != nullptr && s_blob_storage != nullptr;
    }

} ///< namespace disk::storage
