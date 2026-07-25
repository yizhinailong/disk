#pragma once

#include "utils/LogHelper.hpp"

namespace disk::storage {

    inline auto StorageRuntimeLogContext() -> disk::utils::LogContext {
        return disk::utils::LogContext{
            .operation = "storage_runtime",
        };
    }

} // namespace disk::storage
