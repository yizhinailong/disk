/**
 * @file IFileStorage.hpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 文件存储兼容聚合接口定义
 *
 * @copyright Copyright (c) 2026
 *
 */

#pragma once

#include "storage/IBlobStorage.hpp"
#include "storage/IUploadStagingStorage.hpp"

namespace disk::storage {

    /**
     * @brief 文件存储兼容聚合接口
     *
     * 新代码应优先依赖 IUploadStagingStorage 或 IBlobStorage，避免重新耦合
     * 上传暂存区与最终 Blob 存储边界。
     */
    class IFileStorage : public IUploadStagingStorage, public IBlobStorage {
    public:
        ~IFileStorage() override = default;
    };

} ///< namespace disk::storage
