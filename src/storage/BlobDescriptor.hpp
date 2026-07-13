/**
 * @file BlobDescriptor.hpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 最终内容 Blob 描述符
 *
 * @copyright Copyright (c) 2026
 *
 */

#pragma once

#include <cstdint>
#include <string>

namespace disk::storage {

    /**
     * @brief 最终内容 Blob 的稳定描述符
     *
     * @details
     * 仅携带定位最终内容所需的存储无关元数据；本地路径解析由存储实现负责。
     */
    struct BlobDescriptor {
        uint64_t content_id{ 0 };
        std::string hash_md5;
        uint64_t size{ 0 };
    };

} ///< namespace disk::storage
