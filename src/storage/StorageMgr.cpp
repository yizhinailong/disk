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

    void StorageMgr::SetInstance(std::shared_ptr<IFileStorage> storage) {
        s_storage = std::move(storage);
    }

    auto StorageMgr::GetStorage() -> IFileStorage* {
        return s_storage.get();
    }

    auto StorageMgr::IsInitialized() -> bool {
        return s_storage != nullptr;
    }

} ///< namespace disk::storage
