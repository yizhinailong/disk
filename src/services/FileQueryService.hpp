/**
 * @file FileQueryService.hpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 文件查询服务
 *
 * @copyright Copyright (c) 2026
 *
 */

#pragma once

#include <cstdint>
#include <string>

#include <drogon/orm/DbClient.h>

#include "dtos/FileDto.hpp"
#include "services/RedisService.hpp"
#include "utils/ErrorCode.hpp"
#include "utils/LogHelper.hpp"

namespace disk::file {

    /**
     * @brief 文件查询服务类
     *
     * 提供文件查询相关的业务逻辑：
     * - 文件列表、详情、下载
     * - 文件搜索
     */
    class FileQueryService {
    public:
        /**
         * @brief 构造函数
         * @param db_client 数据库客户端
         */
        explicit FileQueryService(drogon::orm::DbClientPtr db_client);
        ~FileQueryService() = default;
        FileQueryService(const FileQueryService&) = delete;
        auto operator=(const FileQueryService&) -> FileQueryService& = delete;
        FileQueryService(FileQueryService&&) = delete;
        auto operator=(FileQueryService&&) -> FileQueryService& = delete;

        /**
         * @brief 获取文件列表
         *
         * 业务规则：
         * - 验证 parent_id 文件夹存在且属于用户（如果 parent_id != 0）
         * - 查询 files 和 folders 表，合并结果
         * - 应用 type 过滤（all/file/folder）
         * - 应用排序和分页
         *
         * @param request 文件列表请求
         * @param user_id 用户 ID
         * @param log_context 请求日志上下文
         * @return drogon::Task<Result<FileListResponse>> 成功返回文件列表，失败返回错误
         */
        [[nodiscard]]
        auto GetFileList(
            FileListRequest request,
            uint64_t user_id,
            disk::utils::LogContext log_context = {}
        )
            -> drogon::Task<Result<FileListResponse>>;

        /**
         * @brief 获取文件详情
         *
         * @param file_id 文件 ID
         * @param user_id 用户 ID
         * @param log_context 请求日志上下文
         * @return drogon::Task<Result<FileDetailResponse>> 成功返回文件详情，失败返回错误
         */
        [[nodiscard]]
        auto GetFileDetail(
            uint64_t file_id,
            uint64_t user_id,
            disk::utils::LogContext log_context = {}
        )
            -> drogon::Task<Result<FileDetailResponse>>;

        /**
         * @brief 获取下载信息（元数据）
         *
         * 业务规则：
         * - 验证文件存在且属于用户
         * - 关联 file_contents 获取存储信息
         * - 返回文件元数据（不含物理路径）
         *
         * @param file_id 文件 ID
         * @param user_id 用户 ID
         * @return drogon::Task<Result<DownloadInfoResponse>> 成功返回下载信息，失败返回错误
         */
        [[nodiscard]]
        auto GetDownloadInfo(
            uint64_t file_id,
            uint64_t user_id,
            disk::utils::LogContext log_context = {}
        )
            -> drogon::Task<Result<DownloadInfoResponse>>;

        /**
         * @brief 获取下载数据（含最终 Blob 描述符）
         *
         * 业务规则：
         * - 验证文件存在且属于用户
         * - 关联 file_contents 获取最终 Blob 描述符
         * - 返回完整下载信息（不含物理路径）
         *
         * @param file_id 文件 ID
         * @param user_id 用户 ID
         * @return drogon::Task<Result<DownloadInfo>> 成功返回下载数据，失败返回错误
         */
        [[nodiscard]]
        auto GetDownloadData(
            uint64_t file_id,
            uint64_t user_id,
            disk::utils::LogContext log_context = {}
        )
            -> drogon::Task<Result<DownloadInfo>>;

        /**
         * @brief 更新成功下载后的文件元数据
         *
         * @param file_id 文件 ID
         * @param user_id 用户 ID
         * @return drogon::Task<void>
         */
        auto UpdateDownloadMetadata(
            uint64_t file_id,
            uint64_t user_id,
            disk::utils::LogContext log_context = {}
        ) -> drogon::Task<void>;

        /**
         * @brief 搜索文件和文件夹
         *
         * 业务规则：
         * - 支持文件名模糊搜索（LIKE %keyword%）
         * - 支持按类型过滤（all/file/folder）
         * - 支持限定搜索范围（folder_id）
         * - 返回结果包含路径面包屑信息
         * - 应用分页
         *
         * @param request 搜索请求
         * @param user_id 用户 ID
         * @param log_context 请求日志上下文
         * @return drogon::Task<Result<SearchResponse>> 成功返回搜索结果，失败返回错误
         */
        [[nodiscard]]
        auto Search(
            SearchRequest request,
            uint64_t user_id,
            disk::utils::LogContext log_context = {}
        )
            -> drogon::Task<Result<SearchResponse>>;

    private:
        drogon::orm::DbClientPtr m_db_client;                                                                         ///< 数据库客户端
        std::shared_ptr<disk::services::RedisService> m_redis_service{ disk::services::RedisService::GetInstance() }; ///< Redis 服务
    };

} // namespace disk::file
