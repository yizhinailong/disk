/**
 * @file FileListQuery.hpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 文件列表查询对象
 *
 * @copyright Copyright (c) 2026
 *
 */

#pragma once

#include <cstdint>
#include <utility>
#include <vector>

#include <drogon/orm/DbClient.h>

#include "dtos/FileDto.hpp"

namespace disk::file {

    /**
     * @brief 文件列表查询对象
     *
     * 封装文件列表的计数、分页 SQL、确定性排序和行映射。
     */
    class FileListQuery {
    public:
        explicit FileListQuery(drogon::orm::DbClientPtr db_client);

        [[nodiscard]]
        auto Execute(FileListQueryParams request, uint64_t user_id)
            -> drogon::Task<FileListResponse>;

    private:
        [[nodiscard]]
        auto queryAll(FileListQueryParams const& request, uint64_t user_id, int64_t offset)
            -> drogon::Task<std::pair<std::vector<FileListItem>, int>>;

        [[nodiscard]]
        auto queryFiles(FileListQueryParams const& request, uint64_t user_id, int64_t offset)
            -> drogon::Task<std::pair<std::vector<FileListItem>, int>>;

        [[nodiscard]]
        auto queryFolders(FileListQueryParams const& request, uint64_t user_id, int64_t offset)
            -> drogon::Task<std::pair<std::vector<FileListItem>, int>>;

        drogon::orm::DbClientPtr m_db_client;
    };

} ///< namespace disk::file
