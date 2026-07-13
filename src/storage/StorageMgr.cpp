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
    std::shared_ptr<UploadStagingStorage> StorageMgr::s_upload_staging_storage = nullptr;
    std::shared_ptr<BlobStore> StorageMgr::s_blob_store = nullptr;

    void StorageMgr::SetInstance(std::shared_ptr<IFileStorage> storage) {
        s_storage = std::move(storage);
        s_upload_staging_storage = s_storage;
        s_blob_store = s_storage;
    }

    void StorageMgr::SetInstance(
        std::shared_ptr<UploadStagingStorage> upload_staging_storage,
        std::shared_ptr<BlobStore> blob_store
    ) {
        s_storage = nullptr;
        s_upload_staging_storage = std::move(upload_staging_storage);
        s_blob_store = std::move(blob_store);
    }

    auto StorageMgr::GetUploadStagingStorage() -> UploadStagingStorage* {
        return s_upload_staging_storage.get();
    }

    auto StorageMgr::GetBlobStore() -> BlobStore* {
        return s_blob_store.get();
    }

    auto StorageMgr::GetStorage() -> IFileStorage* {
        return s_storage.get();
    }

    auto StorageMgr::IsInitialized() -> bool {
        return s_upload_staging_storage != nullptr && s_blob_store != nullptr;
    }

} ///< namespace disk::storage
