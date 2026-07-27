/**
 * @file BlobStoreMgr.cpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 最终内容 Blob 存储管理器实现
 *
 * @copyright Copyright (c) 2026
 *
 */

#include "BlobStoreMgr.hpp"

namespace disk::storage {

    std::shared_ptr<IBlobStore> BlobStoreMgr::s_blob_store = nullptr;

    void BlobStoreMgr::SetInstance(std::shared_ptr<IBlobStore> blob_store) {
        s_blob_store = std::move(blob_store);
    }

    auto BlobStoreMgr::GetBlobStore() -> IBlobStore* {
        return s_blob_store.get();
    }

} // namespace disk::storage
