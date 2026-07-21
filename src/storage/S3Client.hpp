#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "storage/IBlobStore.hpp"
#include "utils/ConfigMgr.hpp"
#include "utils/ErrorCode.hpp"
#include "utils/LogHelper.hpp"

namespace disk::storage {

    enum class S3SdkOperation : uint8_t {
        GetBucketLocation,
        HeadObject,
        PutObject,
        DeleteObject,
        GetObject,
        ListObjectsV2,
        DeleteObjects,
        CreateMultipartUpload,
        UploadPart,
        UploadPartCopy,
        CompleteMultipartUpload,
        AbortMultipartUpload,
    };

    enum class S3SdkOutcome : uint8_t {
        Success,
        Timeout,
        Connection,
        Conflict,
        NotFound,
        Retryable,
        Permanent,
        Protocol,
        Other,
    };

    [[nodiscard]]
    auto S3SdkOperationName(S3SdkOperation operation) noexcept -> std::string_view;

    [[nodiscard]]
    auto S3SdkOutcomeName(S3SdkOutcome outcome) noexcept -> std::string_view;

    auto RecordS3SdkCallResult(
        const disk::utils::LogContext& log_context,
        S3SdkOperation operation,
        S3SdkOutcome outcome
    ) noexcept -> void;

    enum class S3FailureClass {
        Permanent,
        Retryable,
    };

    [[nodiscard]]
    auto ClassifyS3Failure(
        int http_status,
        std::string_view exception_name,
        bool sdk_retryable
    ) noexcept -> S3FailureClass;

    [[nodiscard]]
    auto ShouldRetryS3Failure(
        int http_status,
        std::string_view exception_name,
        bool sdk_retryable,
        long attempted_retries,
        long max_retries
    ) noexcept -> bool;

    struct S3HeadObjectResult {
        bool exists{ false };
        uint64_t size{ 0 };
        std::string etag;
    };

    struct S3PutObjectResult {
        std::string etag;
        bool created{ false };
    };

    struct S3ListObjectsResult {
        std::vector<std::string> keys;
        std::string continuation_token;
        bool is_truncated{ false };
    };

    struct S3CompletedPart {
        int part_number{ 0 };
        std::string etag;
    };

    struct S3CompleteMultipartResult {
        bool created{ false };
    };

    class IS3Client {
    public:
        virtual ~IS3Client() = default;

        [[nodiscard]]
        virtual auto ValidateBucketAccessible(disk::utils::LogContext log_context = {})
            -> Result<void> = 0;

        [[nodiscard]]
        virtual auto HeadObject(
            const std::string& key,
            disk::utils::LogContext log_context = {}
        ) -> Result<S3HeadObjectResult> = 0;

        [[nodiscard]]
        virtual auto PutObjectIfAbsent(
            const std::string& key,
            std::string data,
            disk::utils::LogContext log_context = {}
        )
            -> Result<S3PutObjectResult> = 0;

        [[nodiscard]]
        virtual auto PutObjectFromFileIfAbsent(
            const std::string& key,
            const std::filesystem::path& local_path,
            disk::utils::LogContext log_context = {}
        ) -> Result<S3PutObjectResult> = 0;

        [[nodiscard]]
        virtual auto DeleteObject(
            const std::string& key,
            disk::utils::LogContext log_context = {}
        ) -> Result<void> = 0;

        [[nodiscard]]
        virtual auto GetObjectRange(
            const std::string& key,
            uint64_t start,
            uint64_t length,
            disk::utils::LogContext log_context = {}
        )
            -> Result<std::shared_ptr<StorageReadStream>> = 0;

        [[nodiscard]]
        virtual auto ListObjects(
            const std::string& prefix,
            const std::string& continuation_token,
            uint32_t max_keys,
            disk::utils::LogContext log_context = {}
        ) -> Result<S3ListObjectsResult> = 0;

        [[nodiscard]]
        virtual auto DeleteObjects(
            const std::vector<std::string>& keys,
            disk::utils::LogContext log_context = {}
        ) -> Result<void> = 0;

        [[nodiscard]]
        virtual auto CreateMultipartUpload(
            const std::string& key,
            disk::utils::LogContext log_context = {}
        ) -> Result<std::string> = 0;

        [[nodiscard]]
        virtual auto UploadPart(
            const std::string& key,
            const std::string& upload_id,
            int part_number,
            std::string data,
            disk::utils::LogContext log_context = {}
        ) -> Result<std::string> = 0;

        [[nodiscard]]
        virtual auto UploadPartCopy(
            const std::string& source_key,
            const std::string& destination_key,
            const std::string& upload_id,
            int part_number,
            uint64_t start,
            uint64_t length,
            disk::utils::LogContext log_context = {}
        ) -> Result<std::string> = 0;

        [[nodiscard]]
        virtual auto CompleteMultipartUpload(
            const std::string& key,
            const std::string& upload_id,
            const std::vector<S3CompletedPart>& parts,
            bool only_if_absent,
            disk::utils::LogContext log_context = {}
        ) -> Result<S3CompleteMultipartResult> = 0;

        [[nodiscard]]
        virtual auto AbortMultipartUpload(
            const std::string& key,
            const std::string& upload_id,
            disk::utils::LogContext log_context = {}
        )
            -> Result<void> = 0;
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
        auto ValidateBucketAccessible(disk::utils::LogContext log_context = {})
            -> Result<void> override;

        [[nodiscard]]
        auto HeadObject(
            const std::string& key,
            disk::utils::LogContext log_context = {}
        ) -> Result<S3HeadObjectResult> override;

        [[nodiscard]]
        auto PutObjectIfAbsent(
            const std::string& key,
            std::string data,
            disk::utils::LogContext log_context = {}
        )
            -> Result<S3PutObjectResult> override;

        [[nodiscard]]
        auto PutObjectFromFileIfAbsent(
            const std::string& key,
            const std::filesystem::path& local_path,
            disk::utils::LogContext log_context = {}
        ) -> Result<S3PutObjectResult> override;

        [[nodiscard]]
        auto DeleteObject(
            const std::string& key,
            disk::utils::LogContext log_context = {}
        ) -> Result<void> override;

        [[nodiscard]]
        auto GetObjectRange(
            const std::string& key,
            uint64_t start,
            uint64_t length,
            disk::utils::LogContext log_context = {}
        )
            -> Result<std::shared_ptr<StorageReadStream>> override;

        [[nodiscard]]
        auto ListObjects(
            const std::string& prefix,
            const std::string& continuation_token,
            uint32_t max_keys,
            disk::utils::LogContext log_context = {}
        ) -> Result<S3ListObjectsResult> override;

        [[nodiscard]]
        auto DeleteObjects(
            const std::vector<std::string>& keys,
            disk::utils::LogContext log_context = {}
        ) -> Result<void> override;

        [[nodiscard]]
        auto CreateMultipartUpload(
            const std::string& key,
            disk::utils::LogContext log_context = {}
        ) -> Result<std::string> override;

        [[nodiscard]]
        auto UploadPart(
            const std::string& key,
            const std::string& upload_id,
            int part_number,
            std::string data,
            disk::utils::LogContext log_context = {}
        ) -> Result<std::string> override;

        [[nodiscard]]
        auto UploadPartCopy(
            const std::string& source_key,
            const std::string& destination_key,
            const std::string& upload_id,
            int part_number,
            uint64_t start,
            uint64_t length,
            disk::utils::LogContext log_context = {}
        ) -> Result<std::string> override;

        [[nodiscard]]
        auto CompleteMultipartUpload(
            const std::string& key,
            const std::string& upload_id,
            const std::vector<S3CompletedPart>& parts,
            bool only_if_absent,
            disk::utils::LogContext log_context = {}
        ) -> Result<S3CompleteMultipartResult> override;

        [[nodiscard]]
        auto AbortMultipartUpload(
            const std::string& key,
            const std::string& upload_id,
            disk::utils::LogContext log_context = {}
        )
            -> Result<void> override;

    private:
        class Impl;
        std::unique_ptr<Impl> m_impl;
    };

} // namespace disk::storage
