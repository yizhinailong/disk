/**
 * @file DownloadResponder.hpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 统一下载响应构造工具
 *
 * @details
 * 提取 FileController::Download() 和 ShareController::Download() 的公共下载响应逻辑，
 * 实现 Range 请求解析、416/206/200 三分支响应构造的统一入口。
 *
 * @copyright Copyright (c) 2026
 *
 */

#pragma once

#include <cstdint>
#include <string>

#include <drogon/HttpResponse.h>
#include <drogon/utils/coroutine.h>

#include "storage/IFileStorage.hpp"

namespace disk::controllers {

    struct DownloadParams {
        std::string storage_path;
        std::string filename;
        uint64_t file_size{ 0 };
        std::string mime_type;
        std::string file_hash;
        std::string range_header;
    };

    [[nodiscard]]
    auto BuildDownloadResponse(
        const DownloadParams& params,
        disk::storage::IFileStorage* storage
    ) -> drogon::Task<drogon::HttpResponsePtr>;

} // namespace disk::controllers
