/**
 * @file ApplicationContext.cpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 应用服务组合上下文实现
 *
 * @copyright Copyright (c) 2026
 *
 */

#include "application/ApplicationContext.hpp"

#include <utility>

#include "services/ObservedDbClient.hpp"
#include "storage/BlobStoreMgr.hpp"
#include "storage/StorageMgr.hpp"
#include "utils/ConfigMgr.hpp"

namespace disk::application {

    auto ApplicationContext::Initialize(
        drogon::orm::DbClientPtr db_client,
        drogon::nosql::RedisClientPtr redis_client,
        disk::storage::UploadStagingStorage* upload_staging_storage,
        disk::storage::IBlobStore* blob_store,
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
        disk::storage::IBlobStore* blob_store,
        std::string jwt_secret
    ) -> void {
        if (m_upload_service) {
            return;
        }

        m_blob_store = blob_store;

        m_download_integrity_service =
            std::make_unique<disk::download::DownloadIntegrityService>(db_client, m_blob_store);

        m_upload_service = std::make_unique<disk::file::UploadService>(
            db_client,
            upload_staging_storage,
            m_blob_store
        );
        m_file_query_service = std::make_unique<disk::file::FileQueryService>(db_client);
        m_file_mutation_service = std::make_unique<disk::file::FileMutationService>(db_client);
        m_folder_service = std::make_unique<disk::folder::FolderService>(db_client);
        m_share_service = std::make_unique<disk::share::ShareService>(
            db_client,
            redis_client,
            std::move(jwt_secret)
        );
        m_cleanup_service = std::make_shared<disk::services::CleanupService>(db_client);
    }

    auto ApplicationContext::ensureInitialized() -> void {
        if (m_upload_service) {
            return;
        }

        initialize(
            disk::metrics::ObserveDbClient(drogon::app().getDbClient()),
            drogon::app().getRedisClient(),
            disk::storage::StorageMgr::GetUploadStagingStorage(),
            disk::storage::BlobStoreMgr::GetBlobStore(),
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

    auto ApplicationContext::BlobStore() -> disk::storage::IBlobStore* {
        ensureInitialized();
        return m_blob_store;
    }

    auto ApplicationContext::DownloadIntegrity()
        -> disk::download::IDownloadIntegrityService* {
        ensureInitialized();
        return m_download_integrity_service.get();
    }

} // namespace disk::application
