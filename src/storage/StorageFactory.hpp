#pragma once

#include <memory>

#include "storage/IFileStorage.hpp"
#include "utils/ConfigMgr.hpp"

namespace disk::storage {

    class StorageFactory {
    public:
        [[nodiscard]]
        static auto Create(std::shared_ptr<disk::utils::ConfigMgr> config_mgr)
            -> std::shared_ptr<IFileStorage>;
    };

} ///< namespace disk::storage
