#pragma once

#include <functional>
#include <memory>

#include "storage/IBlobStore.hpp"
#include "storage/IFileStorage.hpp"
#include "storage/S3Client.hpp"
#include "utils/ConfigMgr.hpp"

namespace disk::storage {

    struct StorageBundle {
        std::shared_ptr<IFileStorage> storage;
        std::shared_ptr<IBlobStore> blob_store;
    };

    class StorageFactory {
    public:
        using S3ClientFactory = std::function<std::shared_ptr<IS3Client>(const disk::utils::S3StorageConfig&)>;

        [[nodiscard]]
        static auto Create(
            std::shared_ptr<disk::utils::ConfigMgr> config_mgr,
            S3ClientFactory s3_client_factory = {}
        ) -> StorageBundle;
    };

} // namespace disk::storage
