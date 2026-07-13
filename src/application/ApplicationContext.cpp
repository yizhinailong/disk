/**
 * @file ApplicationContext.cpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 应用服务组合上下文实现
 *
 * @copyright Copyright (c) 2026
 *
 */

#include "application/ApplicationContext.hpp"

#include "storage/StorageMgr.hpp"
#include "utils/ConfigMgr.hpp"

#include <utility>

namespace disk::application {

    auto ApplicationContext::Initialize(
        drogon::orm::DbClientPtr db_client,
        drogon::nosql::RedisClientPtr redis_client,
        disk::storage::UploadStagingStorage* upload_staging_storage,
        disk::storage::BlobStore* blob_store,
        std::string jwt_secret
    ) -> void {
        GetInstance()->initialize(
            std::move(db_client),
            std::move(redis_client),
            upload_staging_storage,
            blob_store,
            std::move(jwt_secret)
        );
    }

    auto ApplicationContext::initialize(
        drogon::orm::DbClientPtr db_client,
        drogon::nosql::RedisClientPtr redis_client,
        disk::storage::UploadStagingStorage* upload_staging_storage,
        disk::storage::BlobStore* blob_store,
        std::string jwt_secret
    ) -> void {
        if (m_upload_service) {
            return;
        }

        m_db_client = std::move(db_client);
        m_redis_client = std::move(redis_client);
        m_upload_staging_storage = upload_staging_storage;
        m_blob_store = blob_store;

        m_upload_service = std::make_unique<disk::file::UploadService>(
            m_db_client,
            m_upload_staging_storage,
            m_blob_store
        );
        m_file_query_service = std::make_unique<disk::file::FileQueryService>(m_db_client);
        m_file_mutation_service = std::make_unique<disk::file::FileMutationService>(m_db_client);
        m_folder_service = std::make_unique<disk::folder::FolderService>(m_db_client);
        m_share_service = std::make_unique<disk::share::ShareService>(
            m_db_client,
            m_redis_client,
            std::move(jwt_secret)
        );
        m_cleanup_service = std::make_shared<disk::services::CleanupService>(m_db_client);
    }

    auto ApplicationContext::ensureInitialized() -> void {
        if (m_upload_service) {
            return;
        }

        initialize(
            drogon::app().getDbClient(),
            drogon::app().getRedisClient(),
            disk::storage::StorageMgr::GetUploadStagingStorage(),
            disk::storage::StorageMgr::GetBlobStore(),
            disk::utils::ConfigMgr::GetInstance()->GetJwtSecret()
        );
    }

    auto ApplicationContext::Upload() -> disk::file::UploadService& {
        ensureInitialized();
        return *m_upload_service;
    }

    auto ApplicationContext::FileQuery() -> disk::file::FileQueryService& {
        ensureInitialized();
        return *m_file_query_service;
    }

    auto ApplicationContext::FileMutation() -> disk::file::FileMutationService& {
        ensureInitialized();
        return *m_file_mutation_service;
    }

    auto ApplicationContext::Folder() -> disk::folder::FolderService& {
        ensureInitialized();
        return *m_folder_service;
    }

    auto ApplicationContext::Share() -> disk::share::ShareService& {
        ensureInitialized();
        return *m_share_service;
    }

    auto ApplicationContext::Cleanup() -> disk::services::CleanupService& {
        ensureInitialized();
        return *m_cleanup_service;
    }

    auto ApplicationContext::UploadStagingStorage() -> disk::storage::UploadStagingStorage* {
        ensureInitialized();
        return m_upload_staging_storage;
    }

    auto ApplicationContext::BlobStore() -> disk::storage::BlobStore* {
        ensureInitialized();
        return m_blob_store;
    }

} ///< namespace disk::application
