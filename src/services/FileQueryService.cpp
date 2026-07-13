/**
 * @file FileQueryService.cpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 文件查询服务实现
 *
 * @copyright Copyright (c) 2026
 *
 */

#include "FileQueryService.hpp"

#include <string>

#include "FileListQuery.hpp"
#include "SearchQuery.hpp"

#include <sstream>

#include <json/reader.h>
#include <json/writer.h>

#include "FileServiceUtils.hpp"
#include "models/FileContents.hpp"
#include "models/Files.hpp"
#include "models/Folders.hpp"
#include "utils/RedisKeyPrefix.hpp"

namespace disk::file {

    using drogon::orm::CompareOperator;
    using drogon::orm::CoroMapper;
    using drogon::orm::Criteria;
    using drogon_model::disk::FileContents;
    using drogon_model::disk::Files;
    using drogon_model::disk::Folders;

    /// ==================== 构造函数 ====================

    FileQueryService::FileQueryService(drogon::orm::DbClientPtr db_client)
        : m_db_client(std::move(db_client)) {
        Logger::Debug() << "FileQueryService initialization completed";
    }

    /// ==================== GetFileList ====================

    auto FileQueryService::GetFileList(FileListRequest request, uint64_t user_id)
        -> drogon::Task<Result<FileListResponse>> {
        const auto& query_params = request.query;

        Logger::Debug() << "Starting get file list: parent_id=" << query_params.parent_id
                  << ", page=" << query_params.page << ", page_size=" << query_params.page_size
                  << ", sort_by=" << query_params.sort_by << ", sort_order=" << query_params.sort_order
                  << ", type=" << query_params.type << ", user_id=" << user_id;

        /// 0. Try Redis cache
        auto cache_key = disk::redis::RedisKeyPrefix::BuildFileListCacheKey(
            user_id, query_params.parent_id, query_params.type,
            query_params.sort_by, query_params.sort_order, query_params.page, query_params.page_size
        );

        auto cache_result = co_await m_redis_service->Get(cache_key);
        if (cache_result.has_value()) {
            Logger::Debug() << "File list cache hit: key=" << cache_key;
            Json::Value cached_json;
            Json::CharReaderBuilder builder;
            std::istringstream stream(*cache_result);
            std::string errors;
            if (Json::parseFromStream(builder, stream, &cached_json, &errors)) {
                co_return FileListResponse::FromJson(cached_json);
            }
            Logger::Warn() << "File list cache parse error: " << errors;
        }
        Logger::Debug() << "File list cache miss: key=" << cache_key;

        /// 1. 验证 parent_id 文件夹存在且属于用户（如果 parent_id != 0）
        if (query_params.parent_id != 0) {
            try {
                CoroMapper<Folders> folder_mapper(m_db_client);
                auto folder = co_await folder_mapper.findOne(
                    Criteria(Folders::Cols::_id, CompareOperator::EQ, query_params.parent_id) &&
                    Criteria(Folders::Cols::_user_id, CompareOperator::EQ, user_id)
                );
                Logger::Debug() << "Folder verification passed: folder_id=" << query_params.parent_id;
            } catch (const drogon::orm::DrogonDbException& e) {
                Logger::Warn() << "Folder not found or no permission: folder_id=" << query_params.parent_id;
                co_return std::unexpected(ErrorInfo(ErrorCode::FolderNotFound));
            }
        }

        /// 2. 使用查询对象执行文件列表 SQL（JOIN 消除 N+1，LIMIT/OFFSET 消除内存分页）
        FileListResponse response;
        try {
            FileListQuery query(m_db_client);
            response = co_await query.Execute(query_params, user_id);
        } catch (const drogon::orm::DrogonDbException& e) {
            Logger::Warn() << "Failed to query file list: " << e.base().what();
        }

        Logger::Debug() << "File list retrieved successfully: total=" << response.pagination.total
                  << ", page=" << query_params.page;

        /// Cache the response
        {
            auto response_json = response.ToJson();
            Json::StreamWriterBuilder writer_builder;
            writer_builder["indentation"] = "";
            auto serialized = Json::writeString(writer_builder, response_json);
            co_await m_redis_service->Set(cache_key, serialized, 30);
        }

        co_return response;
    }

    /// ==================== GetFileDetail ====================

    auto FileQueryService::GetFileDetail(uint64_t file_id, uint64_t user_id)
        -> drogon::Task<Result<FileDetailResponse>> {

        Logger::Debug() << "Starting get file detail: file_id=" << file_id << ", user_id=" << user_id;

        try {
            CoroMapper<Files> file_mapper(m_db_client);
            auto file = co_await file_mapper.findOne(
                Criteria(Files::Cols::_id, CompareOperator::EQ, file_id) &&
                Criteria(Files::Cols::_user_id, CompareOperator::EQ, user_id)
            );

            std::string hash;
            std::string mime_type = file.getValueOfMimeType();

            if (file.getContentId()) {
                CoroMapper<FileContents> content_mapper(m_db_client);
                auto content = co_await content_mapper.findOne(
                    Criteria(FileContents::Cols::_id, CompareOperator::EQ, *file.getContentId())
                );
                hash = content.getValueOfHashMd5();
                if (mime_type.empty()) {
                    mime_type = content.getValueOfMimeType();
                }
            }

            FileDetailResponse response;
            response.id = file.getValueOfId();
            response.name = file.getValueOfName();
            response.type = "file";
            response.size = file.getValueOfSize();
            response.hash = hash;
            response.mime_type = mime_type;
            response.parent_id = file.getValueOfFolderId();
            response.path = file.getValueOfPath();
            response.created_at = file.getValueOfCreatedAt().toDbStringLocal();
            response.updated_at = file.getValueOfUpdatedAt().toDbStringLocal();

            Logger::Debug() << "File detail retrieved successfully: name=" << response.name;
            co_return response;

        } catch (const drogon::orm::DrogonDbException& e) {
            Logger::Warn() << "File not found or no permission: file_id=" << file_id;
            co_return std::unexpected(ErrorInfo(ErrorCode::FileNotFound));
        }
    }

    /// ==================== GetDownloadInfo ====================

    auto FileQueryService::GetDownloadInfo(uint64_t file_id, uint64_t user_id)
        -> drogon::Task<Result<DownloadInfoResponse>> {

        Logger::Debug() << "Starting get download info: file_id=" << file_id << ", user_id=" << user_id;

        /// 1. 查找文件并验证归属
        try {
            CoroMapper<Files> file_mapper(m_db_client);
            auto file = co_await file_mapper.findOne(
                Criteria(Files::Cols::_id, CompareOperator::EQ, file_id) &&
                Criteria(Files::Cols::_user_id, CompareOperator::EQ, user_id)
            );

            /// 2. 获取文件内容信息
            if (!file.getContentId()) {
                Logger::Error() << "File missing content_id: file_id=" << file_id;
                co_return std::unexpected(
                    ErrorInfo(ErrorCode::FileReadError, "File content info missing")
                );
            }

            CoroMapper<FileContents> content_mapper(m_db_client);
            auto content = co_await content_mapper.findOne(
                Criteria(FileContents::Cols::_id, CompareOperator::EQ, *file.getContentId())
            );

            /// 3. 构造响应
            DownloadInfoResponse response;
            response.file_id = file.getValueOfId();
            response.filename = file.getValueOfName();
            response.file_size = file.getValueOfSize();
            response.file_hash = content.getValueOfHashMd5();
            response.mime_type = file.getValueOfMimeType().empty() ? content.getValueOfMimeType() :
                                                                     file.getValueOfMimeType();
            response.supports_range = true;

            Logger::Debug() << "Download info retrieved successfully: filename=" << response.filename
                      << ", size=" << response.file_size;
            co_return response;

        } catch (const drogon::orm::DrogonDbException& e) {
            Logger::Warn() << "File not found or no permission: file_id=" << file_id;
            co_return std::unexpected(ErrorInfo(ErrorCode::FileNotFound));
        }
    }

    /// ==================== GetDownloadData ====================

    auto FileQueryService::GetDownloadData(uint64_t file_id, uint64_t user_id)
        -> drogon::Task<Result<DownloadInfo>> {

        Logger::Debug() << "Starting get download data: file_id=" << file_id << ", user_id=" << user_id;

        /// 1. 查找文件并验证归属
        try {
            CoroMapper<Files> file_mapper(m_db_client);
            auto file = co_await file_mapper.findOne(
                Criteria(Files::Cols::_id, CompareOperator::EQ, file_id) &&
                Criteria(Files::Cols::_user_id, CompareOperator::EQ, user_id)
            );

            /// 2. 获取文件内容信息
            if (!file.getContentId()) {
                Logger::Error() << "File missing content_id: file_id=" << file_id;
                co_return std::unexpected(
                    ErrorInfo(ErrorCode::FileReadError, "File content info missing")
                );
            }

            CoroMapper<FileContents> content_mapper(m_db_client);
            auto content = co_await content_mapper.findOne(
                Criteria(FileContents::Cols::_id, CompareOperator::EQ, *file.getContentId())
            );

            /// 3. 构造响应
            DownloadInfo info;
            info.file_id = file.getValueOfId();
            info.filename = file.getValueOfName();
            info.file_size = file.getValueOfSize();
            info.file_hash = content.getValueOfHashMd5();
            info.mime_type = file.getValueOfMimeType().empty() ? content.getValueOfMimeType() :
                                                                 file.getValueOfMimeType();
            info.blob = disk::storage::BlobDescriptor{
                .content_id = static_cast<uint64_t>(content.getValueOfId()),
                .hash_md5 = content.getValueOfHashMd5(),
                .size = static_cast<uint64_t>(content.getValueOfSize())
            };
            info.supports_range = true;

            Logger::Debug() << "Download data retrieved successfully: filename=" << info.filename
                      << ", content_id=" << info.blob.content_id;
            co_return info;

        } catch (const drogon::orm::DrogonDbException& e) {
            Logger::Warn() << "File not found or no permission: file_id=" << file_id;
            co_return std::unexpected(ErrorInfo(ErrorCode::FileNotFound));
        }
    }

    /// ==================== UpdateDownloadMetadata ====================

    auto FileQueryService::UpdateDownloadMetadata(uint64_t file_id, uint64_t user_id)
        -> drogon::Task<void> {
        try {
            co_await m_db_client->execSqlCoro(
                "UPDATE files "
                "SET download_count = download_count + 1, last_accessed_at = NOW() "
                "WHERE id = $1 AND user_id = $2",
                file_id,
                user_id
            );
        } catch (const drogon::orm::DrogonDbException& e) {
            Logger::Error() << "Failed to update file download metadata: " << e.base().what()
                            << " (file_id=" << file_id << ", user_id=" << user_id << ")";
        }
    }

    /// ==================== Search ====================

    auto FileQueryService::Search(SearchRequest request, uint64_t user_id)
        -> drogon::Task<Result<SearchResponse>> {
        const auto& query_params = request.query;

        Logger::Debug() << "Starting search file: keyword=\"" << query_params.keyword
                  << "\", type=" << query_params.type << ", folder_id="
                  << (query_params.folder_id.has_value() ? std::to_string(*query_params.folder_id) : "null")
                  << ", page=" << query_params.page << ", page_size=" << query_params.page_size
                  << ", user_id=" << user_id;

        SearchResponse response;
        try {
            SearchQuery query(m_db_client);
            response = co_await query.Execute(query_params, user_id);
        } catch (const drogon::orm::DrogonDbException& e) {
            Logger::Warn() << "Failed to search: " << e.base().what();
        }

        Logger::Debug() << "Search completed: total=" << response.pagination.total << ", page=" << query_params.page;
        co_return response;
    }

} ///< namespace disk::file
