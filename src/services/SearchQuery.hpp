/**
 * @file SearchQuery.hpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 文件搜索查询对象
 *
 * @copyright Copyright (c) 2026
 *
 */

#pragma once

#include <cstdint>

#include <drogon/orm/DbClient.h>

#include "dtos/FileDto.hpp"

namespace disk::file {

    /**
     * @brief 文件搜索查询对象
     *
     * 封装搜索 SQL、过滤、分页和结果行映射。
     */
    class SearchQuery {
    public:
        explicit SearchQuery(drogon::orm::DbClientPtr db_client);

        [[nodiscard]]
        auto Execute(SearchQueryParams request, uint64_t user_id) -> drogon::Task<SearchResponse>;

    private:
        drogon::orm::DbClientPtr m_db_client;
    };

} ///< namespace disk::file
