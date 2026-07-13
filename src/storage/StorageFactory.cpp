#include "storage/StorageFactory.hpp"

#include <stdexcept>
#include <utility>

#include "storage/LocalFileStorage.hpp"
#include "storage/S3Client.hpp"
#include "storage/S3ObjectStorage.hpp"
#include "utils/LogHelper.hpp"

namespace disk::storage {

    auto StorageFactory::Create(std::shared_ptr<disk::utils::ConfigMgr> config_mgr)
        -> std::shared_ptr<IFileStorage> {
        if (config_mgr == nullptr) {
            config_mgr = disk::utils::ConfigMgr::GetInstance();
        }

        switch (config_mgr->GetStorageBackend()) {
            case disk::utils::StorageBackend::Local:
                Logger::Info() << "Initializing local filesystem storage backend";
                return std::make_shared<LocalFileStorage>(std::move(config_mgr));

            case disk::utils::StorageBackend::S3: {
                Logger::Info() << "Initializing S3 object storage backend";
                auto client = std::make_shared<AwsS3Client>(config_mgr->GetS3StorageConfig());
                auto validate_result = client->ValidateBucketAccessible();
                if (!validate_result) {
                    throw std::runtime_error(validate_result.error().message);
                }
                return std::make_shared<S3ObjectStorage>(std::move(config_mgr), std::move(client));
            }
        }

        throw std::runtime_error("Unsupported storage backend");
    }

} ///< namespace disk::storage
