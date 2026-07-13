#pragma once

#include <memory>

#include "storage/IBlobStore.hpp"
#include "storage/IFileStorage.hpp"
#include "utils/ConfigMgr.hpp"

namespace disk::storage {

    struct StorageBundle {
        std::shared_ptr<IFileStorage> storage;
        std::shared_ptr<IBlobStore> blob_store;
    };

    class StorageFactory {
    public:
        [[nodiscard]]
        static auto Create(std::shared_ptr<disk::utils::ConfigMgr> config_mgr) -> StorageBundle;
    };

} ///< namespace disk::storage
