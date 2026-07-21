#pragma once

#include <string>

#include <drogon/utils/coroutine.h>

#include "utils/ErrorCode.hpp"
#include "utils/LogHelper.hpp"

namespace disk::storage {

    struct MultipartUploadDescriptor {
        std::string key;
        std::string upload_id;

        auto operator==(const MultipartUploadDescriptor&) const -> bool = default;
    };

    [[nodiscard]]
    auto BuildMultipartUploadRecoveryId(const MultipartUploadDescriptor& descriptor) -> std::string;

    [[nodiscard]]
    auto BuildMultipartUploadRecoveryDedupeKey(const MultipartUploadDescriptor& descriptor)
        -> std::string;

    class IMultipartUploadJournal {
    public:
        virtual ~IMultipartUploadJournal() = default;

        [[nodiscard]]
        virtual auto Track(const MultipartUploadDescriptor& descriptor) -> Result<void> = 0;

        [[nodiscard]]
        virtual auto Renew(const MultipartUploadDescriptor& descriptor) -> Result<void> = 0;

        [[nodiscard]]
        virtual auto Resolve(const MultipartUploadDescriptor& descriptor) -> Result<void> = 0;

        [[nodiscard]]
        virtual auto ReleaseForRetry(
            const MultipartUploadDescriptor& descriptor,
            const std::string& error
        ) -> Result<void> = 0;
    };

    class IMultipartUploadCleaner {
    public:
        virtual ~IMultipartUploadCleaner() = default;

        [[nodiscard]]
        virtual auto AbortMultipartUpload(
            const MultipartUploadDescriptor& descriptor,
            disk::utils::LogContext log_context = {}
        )
            -> drogon::Task<Result<void>> = 0;
    };

} // namespace disk::storage
