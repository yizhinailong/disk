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

    std::shared_ptr<UploadStagingStorage> StorageMgr::s_upload_staging_storage = nullptr;

    void StorageMgr::SetInstance(std::shared_ptr<UploadStagingStorage> storage) {
        s_upload_staging_storage = std::move(storage);
    }

    auto StorageMgr::GetUploadStagingStorage() -> UploadStagingStorage* {
        return s_upload_staging_storage.get();
    }

    auto StorageMgr::IsInitialized() -> bool {
        return s_upload_staging_storage != nullptr;
    }

} // namespace disk::storage
