/**
 * @file MultipartUploadJournal.hpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief PostgreSQL multipart upload recovery journal
 *
 * @copyright Copyright (c) 2026
 */

#pragma once

#include <cstdint>
#include <string>

#include <drogon/orm/DbClient.h>

#include "storage/MultipartUploadRecovery.hpp"

namespace disk::jobs {

    class PostgresMultipartUploadJournal final : public disk::storage::IMultipartUploadJournal {
    public:
        PostgresMultipartUploadJournal(
            drogon::orm::DbClientPtr db_client,
            std::string instance_id,
            uint32_t lease_duration_seconds
        );

        [[nodiscard]]
        auto Track(const disk::storage::MultipartUploadDescriptor& descriptor)
            -> Result<void> override;

        [[nodiscard]]
        auto Renew(const disk::storage::MultipartUploadDescriptor& descriptor)
            -> Result<void> override;

        [[nodiscard]]
        auto Resolve(const disk::storage::MultipartUploadDescriptor& descriptor)
            -> Result<void> override;

        [[nodiscard]]
        auto ReleaseForRetry(
            const disk::storage::MultipartUploadDescriptor& descriptor,
            const std::string& error
        ) -> Result<void> override;

    private:
        [[nodiscard]]
        auto OwnerFor(const disk::storage::MultipartUploadDescriptor& descriptor) const
            -> std::string;

        drogon::orm::DbClientPtr m_db_client;
        std::string m_owner_prefix;
        uint32_t m_lease_duration_seconds{ 0 };
    };

} // namespace disk::jobs
