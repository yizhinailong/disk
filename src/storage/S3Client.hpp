#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>

#include "storage/IFileStorage.hpp"
#include "utils/ConfigMgr.hpp"
#include "utils/ErrorCode.hpp"

namespace disk::storage {

    struct S3HeadObjectResult {
        bool exists{ false };
        uint64_t size{ 0 };
    };

    class IS3Client {
    public:
        virtual ~IS3Client() = default;

        [[nodiscard]]
        virtual auto ValidateBucketAccessible() -> Result<void> = 0;

        [[nodiscard]]
        virtual auto HeadObject(const std::string& key) -> Result<S3HeadObjectResult> = 0;

        [[nodiscard]]
        virtual auto PutObjectFromFile(const std::string& key, const std::filesystem::path& local_path)
            -> Result<void> = 0;

        [[nodiscard]]
        virtual auto DeleteObject(const std::string& key) -> Result<void> = 0;

        [[nodiscard]]
        virtual auto GetObjectRange(const std::string& key, uint64_t start, uint64_t length)
            -> Result<std::shared_ptr<StorageReadStream>> = 0;
    };

    class AwsS3Client final : public IS3Client {
    public:
        explicit AwsS3Client(disk::utils::S3StorageConfig config);
        ~AwsS3Client() override;

        AwsS3Client(const AwsS3Client&) = delete;
        auto operator=(const AwsS3Client&) -> AwsS3Client& = delete;
        AwsS3Client(AwsS3Client&&) = delete;
        auto operator=(AwsS3Client&&) -> AwsS3Client& = delete;

        [[nodiscard]]
        auto ValidateBucketAccessible() -> Result<void> override;

        [[nodiscard]]
        auto HeadObject(const std::string& key) -> Result<S3HeadObjectResult> override;

        [[nodiscard]]
        auto PutObjectFromFile(const std::string& key, const std::filesystem::path& local_path)
            -> Result<void> override;

        [[nodiscard]]
        auto DeleteObject(const std::string& key) -> Result<void> override;

        [[nodiscard]]
        auto GetObjectRange(const std::string& key, uint64_t start, uint64_t length)
            -> Result<std::shared_ptr<StorageReadStream>> override;

    private:
        class Impl;
        std::unique_ptr<Impl> m_impl;
    };

} ///< namespace disk::storage
