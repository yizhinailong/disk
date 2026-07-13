/**
 * @file IFileStorage.hpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 文件存储实例生命周期边界接口定义
 *
 * @copyright Copyright (c) 2026
 *
 */

#pragma once

namespace disk::storage {

    /**
     * @brief 本地文件存储实例的基础边界
     *
     * 具体存储职责已拆分为：
     * - UploadStagingStorage：上传会话、分片、组装和临时清理
     * - IBlobStore：最终内容 Blob 的提升、读取和删除
     *
     * IFileStorage 仅保留为现有 StorageMgr/ApplicationContext 持有本地存储实例的
     * 稳定类型边界，避免最终 Blob 语义重新泄漏回上传暂存存储。
     */
    class IFileStorage {
    public:
        virtual ~IFileStorage() = default;
    };

} ///< namespace disk::storage
