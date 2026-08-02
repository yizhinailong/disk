#include "storage/StorageFactory.hpp"

#include <stdexcept>
#include <utility>

#include "storage/LocalBlobStore.hpp"
#include "storage/LocalFileStorage.hpp"
#include "storage/S3Client.hpp"
#include "storage/S3ObjectStorage.hpp"
#include "storage/StorageLogContext.hpp"
#include "utils/LogHelper.hpp"

namespace disk::storage {

    auto StorageFactory::Create(
        std::shared_ptr<disk::utils::ConfigMgr> config_mgr,
        S3ClientFactory s3_client_factory
    ) -> StorageBundle {
        if (config_mgr == nullptr) {
            config_mgr = disk::utils::ConfigMgr::GetInstance();
        }

        switch (config_mgr->GetStorageBackend()) {
            case disk::utils::StorageBackend::Local:
                Logger::Info(StorageRuntimeLogContext()) << "Storage backend selected: backend=local";
                return StorageBundle{
                    .upload_staging_storage = std::make_shared<LocalFileStorage>(config_mgr),
                    .blob_store = std::make_shared<LocalBlobStore>(std::move(config_mgr)),
                };

            case disk::utils::StorageBackend::S3: {
                Logger::Info(StorageRuntimeLogContext()) << "Storage backend selected: backend=s3";
                try {
                    const auto s3_config = config_mgr->GetS3StorageConfig();
                    auto client = s3_client_factory ? s3_client_factory(s3_config) : std::make_shared<AwsS3Client>(s3_config);
                    if (client == nullptr) {
                        throw std::runtime_error("S3 client factory returned null");
                    }

                    auto validate_result = client->ValidateBucketAccessible();
                    if (!validate_result) {
                        throw std::runtime_error(validate_result.error().message);
                    }
                    auto s3_storage = std::make_shared<S3ObjectStorage>(std::move(config_mgr), std::move(client));
                    return StorageBundle{
                        .upload_staging_storage = s3_storage,
                        .blob_store = s3_storage,
                    };
                } catch (const std::exception&) {
                    throw std::runtime_error("Failed to initialize S3 storage backend");
                }
            }
        }

        throw std::runtime_error("Unsupported storage backend");
    }

} // namespace disk::storage
