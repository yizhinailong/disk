/**
 * @file ApplicationContext.hpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 应用服务组合上下文
 *
 * @copyright Copyright (c) 2026
 *
 */

#pragma once

#include <memory>

#include <string>

#include <drogon/drogon.h>

#include "services/CleanupService.hpp"
#include "services/FileMutationService.hpp"
#include "services/FileQueryService.hpp"
#include "services/FolderService.hpp"
#include "services/ShareService.hpp"
#include "services/UploadService.hpp"
#include "storage/IBlobStore.hpp"
#include "storage/IFileStorage.hpp"
#include "storage/UploadStagingStorage.hpp"
#include "utils/Singleton.hpp"

namespace disk::application {

    /**
     * @brief 应用级服务组合边界
     *
     * 集中持有 controller 共享的 service 实例，避免 controller 构造函数重复拼装依赖图。
     */
    class ApplicationContext : public disk::utils::Singleton<ApplicationContext> {
        friend class disk::utils::Singleton<ApplicationContext>;

    public:
        static auto Initialize(
            drogon::orm::DbClientPtr db_client,
            drogon::nosql::RedisClientPtr redis_client,
            disk::storage::IFileStorage* storage,
            disk::storage::IBlobStore* blob_store,
            std::string jwt_secret
        ) -> void;

        [[nodiscard]]
        auto Upload() -> disk::file::UploadService&;

        [[nodiscard]]
        auto FileQuery() -> disk::file::FileQueryService&;

        [[nodiscard]]
        auto FileMutation() -> disk::file::FileMutationService&;

        [[nodiscard]]
        auto Folder() -> disk::folder::FolderService&;

        [[nodiscard]]
        auto Share() -> disk::share::ShareService&;

        [[nodiscard]]
        auto Cleanup() -> disk::services::CleanupService&;

        [[nodiscard]]
        auto Storage() -> disk::storage::IFileStorage*;

        [[nodiscard]]
        auto BlobStore() -> disk::storage::IBlobStore*;

        ~ApplicationContext() = default;
        ApplicationContext(const ApplicationContext&) = delete;
        auto operator=(const ApplicationContext&) -> ApplicationContext& = delete;
        ApplicationContext(ApplicationContext&&) = delete;
        auto operator=(ApplicationContext&&) -> ApplicationContext& = delete;

    private:
        ApplicationContext() = default;

        auto initialize(
            drogon::orm::DbClientPtr db_client,
            drogon::nosql::RedisClientPtr redis_client,
            disk::storage::IFileStorage* storage,
            disk::storage::IBlobStore* blob_store,
            std::string jwt_secret
        ) -> void;

        auto ensureInitialized() -> void;

        drogon::orm::DbClientPtr m_db_client{};
        drogon::nosql::RedisClientPtr m_redis_client{};
        disk::storage::IFileStorage* m_storage{};
        disk::storage::UploadStagingStorage* m_upload_staging_storage{};
        disk::storage::IBlobStore* m_blob_store{};

        std::unique_ptr<disk::file::UploadService> m_upload_service{};
        std::unique_ptr<disk::file::FileQueryService> m_file_query_service{};
        std::unique_ptr<disk::file::FileMutationService> m_file_mutation_service{};
        std::unique_ptr<disk::folder::FolderService> m_folder_service{};
        std::unique_ptr<disk::share::ShareService> m_share_service{};
        std::shared_ptr<disk::services::CleanupService> m_cleanup_service{};
    };

} ///< namespace disk::application
