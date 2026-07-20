#include "storage/S3Client.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <sstream>
#include <utility>

#include <aws/core/Aws.h>
#include <aws/core/auth/AWSCredentials.h>
#include <aws/core/auth/AWSCredentialsProvider.h>
#include <aws/core/client/ClientConfiguration.h>
#include <aws/core/client/DefaultRetryStrategy.h>
#include <aws/core/http/HttpResponse.h>
#include <aws/core/utils/memory/stl/AWSStreamFwd.h>
#include <aws/core/utils/memory/stl/AWSStringStream.h>
#include <aws/s3/S3Client.h>
#include <aws/s3/model/AbortMultipartUploadRequest.h>
#include <aws/s3/model/CompleteMultipartUploadRequest.h>
#include <aws/s3/model/CompletedMultipartUpload.h>
#include <aws/s3/model/CompletedPart.h>
#include <aws/s3/model/CreateMultipartUploadRequest.h>
#include <aws/s3/model/Delete.h>
#include <aws/s3/model/DeleteObjectRequest.h>
#include <aws/s3/model/DeleteObjectsRequest.h>
#include <aws/s3/model/GetObjectRequest.h>
#include <aws/s3/model/HeadBucketRequest.h>
#include <aws/s3/model/HeadObjectRequest.h>
#include <aws/s3/model/ListObjectsV2Request.h>
#include <aws/s3/model/ObjectIdentifier.h>
#include <aws/s3/model/PutObjectRequest.h>
#include <aws/s3/model/UploadPartCopyRequest.h>
#include <aws/s3/model/UploadPartRequest.h>

#include "utils/LogHelper.hpp"

namespace disk::storage {

    auto ClassifyS3Failure(
        int http_status,
        std::string_view exception_name,
        bool sdk_retryable
    ) noexcept -> S3FailureClass {
        constexpr std::array<std::string_view, 12> PERMANENT_EXCEPTIONS{
            "AccessDenied",
            "AuthorizationHeaderMalformed",
            "ExpiredToken",
            "InvalidAccessKeyId",
            "InvalidArgument",
            "InvalidBucketName",
            "InvalidRequest",
            "MissingAuthenticationToken",
            "NoSuchBucket",
            "PermanentRedirect",
            "SignatureDoesNotMatch",
            "UnrecognizedClientException",
        };
        constexpr std::array<std::string_view, 13> RETRYABLE_EXCEPTIONS{
            "ConnectionError",
            "InternalError",
            "InternalFailure",
            "NetworkConnection",
            "NetworkingError",
            "RequestTimeout",
            "RequestTimeoutException",
            "ServiceUnavailable",
            "SlowDown",
            "Throttling",
            "ThrottlingException",
            "TooManyRequestsException",
            "TransientError",
        };

        if (std::find(PERMANENT_EXCEPTIONS.begin(), PERMANENT_EXCEPTIONS.end(), exception_name) !=
            PERMANENT_EXCEPTIONS.end()) {
            return S3FailureClass::Permanent;
        }
        if (http_status == 408 || http_status == 429 ||
            (http_status >= 500 && http_status <= 599)) {
            return S3FailureClass::Retryable;
        }
        if (http_status >= 400 && http_status <= 499) {
            return S3FailureClass::Permanent;
        }
        if (std::find(RETRYABLE_EXCEPTIONS.begin(), RETRYABLE_EXCEPTIONS.end(), exception_name) !=
            RETRYABLE_EXCEPTIONS.end()) {
            return S3FailureClass::Retryable;
        }
        return sdk_retryable ? S3FailureClass::Retryable : S3FailureClass::Permanent;
    }

    auto ShouldRetryS3Failure(
        int http_status,
        std::string_view exception_name,
        bool sdk_retryable,
        long attempted_retries,
        long max_retries
    ) noexcept -> bool {
        return attempted_retries < max_retries &&
               ClassifyS3Failure(http_status, exception_name, sdk_retryable) ==
                   S3FailureClass::Retryable;
    }

    namespace {
        constexpr const char* AWS_ALLOC_TAG = "disk-s3-storage";
        constexpr uint32_t MAX_DELETE_OBJECTS = 1000;
        constexpr int MIN_MULTIPART_PART_NUMBER = 1;
        constexpr int MAX_MULTIPART_PART_NUMBER = 10000;

        class DiskS3RetryStrategy final : public Aws::Client::DefaultRetryStrategy {
        public:
            DiskS3RetryStrategy(long max_retries, long scale_factor)
                : Aws::Client::DefaultRetryStrategy(max_retries, scale_factor) {}

            auto ShouldRetry(
                const Aws::Client::AWSError<Aws::Client::CoreErrors>& error,
                long attempted_retries
            ) const -> bool override {
                const auto& exception_name = error.GetExceptionName();
                return ShouldRetryS3Failure(
                    static_cast<int>(error.GetResponseCode()),
                    std::string_view(exception_name.data(), exception_name.size()),
                    error.ShouldRetry(),
                    attempted_retries,
                    m_maxRetries
                );
            }

            auto GetStrategyName() const -> const char* override {
                return "disk-s3";
            }
        };

        auto IsNotFoundError(const Aws::S3::S3Error& error) -> bool {
            const auto response_code = error.GetResponseCode();
            const auto exception_name = error.GetExceptionName();
            return response_code == Aws::Http::HttpResponseCode::NOT_FOUND ||
                   exception_name == "NoSuchKey" || exception_name == "NoSuchUpload" ||
                   exception_name == "NotFound";
        }

        auto IsPreconditionFailedError(const Aws::S3::S3Error& error) -> bool {
            return error.GetResponseCode() == Aws::Http::HttpResponseCode::PRECONDITION_FAILED ||
                   error.GetExceptionName() == "PreconditionFailed";
        }

        auto ToErrorInfo(const Aws::S3::S3Error& error, ErrorCode code, const std::string& operation)
            -> ErrorInfo {
            std::string message = operation + " failed";
            if (!error.GetMessage().empty()) {
                message += ": ";
                message += error.GetMessage();
            }
            return ErrorInfo(code, message);
        }

        auto ValidatePartNumber(int part_number) -> Result<void> {
            if (part_number < MIN_MULTIPART_PART_NUMBER || part_number > MAX_MULTIPART_PART_NUMBER) {
                return std::unexpected(
                    ErrorInfo(ErrorCode::ValidationFailed, "Invalid S3 multipart part number")
                );
            }
            return {};
        }

        class AwsApiRuntime final {
        public:
            AwsApiRuntime() {
                Aws::InitAPI(m_options);
            }

            ~AwsApiRuntime() {
                Aws::ShutdownAPI(m_options);
            }

            AwsApiRuntime(const AwsApiRuntime&) = delete;
            auto operator=(const AwsApiRuntime&) -> AwsApiRuntime& = delete;

        private:
            Aws::SDKOptions m_options{};
        };

        class S3ObjectReadStream final : public StorageReadStream {
        public:
            S3ObjectReadStream(Aws::S3::Model::GetObjectResult result, uint64_t remaining)
                : m_result(std::move(result)), m_body(&m_result.GetBody()), m_remaining(remaining) {}

            [[nodiscard]]
            auto Read(char* buffer, std::size_t length) -> std::size_t override {
                if (buffer == nullptr || m_body == nullptr || m_remaining == 0) {
                    return 0;
                }

                const auto bounded_remaining = static_cast<std::size_t>(std::min<uint64_t>(
                    m_remaining,
                    static_cast<uint64_t>(std::numeric_limits<std::size_t>::max())
                ));
                const auto read_size = std::min(length, bounded_remaining);
                m_body->read(buffer, static_cast<std::streamsize>(read_size));
                const auto read_bytes = static_cast<std::size_t>(m_body->gcount());
                if (read_bytes == 0) {
                    m_remaining = 0;
                    return 0;
                }

                m_remaining -= read_bytes;
                return read_bytes;
            }

            auto Close() -> void override {
                m_remaining = 0;
            }

        private:
            Aws::S3::Model::GetObjectResult m_result;
            Aws::IOStream* m_body{ nullptr };
            uint64_t m_remaining{ 0 };
        };
    } // namespace

    class AwsS3Client::Impl {
    public:
        explicit Impl(disk::utils::S3StorageConfig config)
            : runtime(std::make_shared<AwsApiRuntime>()), storage_config(std::move(config)) {
            Aws::Client::ClientConfiguration client_config;
            client_config.region = storage_config.region;
            client_config.scheme = storage_config.use_ssl ? Aws::Http::Scheme::HTTPS : Aws::Http::Scheme::HTTP;
            client_config.verifySSL = storage_config.verify_ssl;
            client_config.maxConnections = storage_config.max_connections;
            client_config.connectTimeoutMs = storage_config.connect_timeout_ms;
            client_config.requestTimeoutMs = storage_config.request_timeout_ms;
            client_config.retryStrategy = std::make_shared<DiskS3RetryStrategy>(
                storage_config.max_retries,
                storage_config.retry_base_delay_ms
            );
            if (!storage_config.endpoint.empty()) {
                client_config.endpointOverride = storage_config.endpoint;
            }

            const auto* access_key = std::getenv("DISK_S3_ACCESS_KEY");
            const auto* secret_key = std::getenv("DISK_S3_SECRET_KEY");
            const auto* session_token = std::getenv("DISK_S3_SESSION_TOKEN");
            const auto use_virtual_addressing = !storage_config.force_path_style;

            if (access_key != nullptr && secret_key != nullptr && std::string(access_key).size() > 0 &&
                std::string(secret_key).size() > 0) {
                Aws::Auth::AWSCredentials credentials(
                    access_key,
                    secret_key,
                    session_token == nullptr ? "" : session_token
                );
                client = std::make_unique<Aws::S3::S3Client>(
                    credentials,
                    client_config,
                    Aws::Client::AWSAuthV4Signer::PayloadSigningPolicy::Never,
                    use_virtual_addressing
                );
            } else {
                client = std::make_unique<Aws::S3::S3Client>(
                    client_config,
                    Aws::Client::AWSAuthV4Signer::PayloadSigningPolicy::Never,
                    use_virtual_addressing
                );
            }
        }

        std::shared_ptr<AwsApiRuntime> runtime;
        disk::utils::S3StorageConfig storage_config;
        std::unique_ptr<Aws::S3::S3Client> client;
    };

    AwsS3Client::AwsS3Client(disk::utils::S3StorageConfig config)
        : m_impl(std::make_unique<Impl>(std::move(config))) {}

    AwsS3Client::~AwsS3Client() = default;

    auto AwsS3Client::ValidateBucketAccessible() -> Result<void> {
        Aws::S3::Model::HeadBucketRequest request;
        request.SetBucket(m_impl->storage_config.bucket);

        auto outcome = m_impl->client->HeadBucket(request);
        if (!outcome.IsSuccess()) {
            return std::unexpected(ToErrorInfo(
                outcome.GetError(),
                ErrorCode::InternalError,
                "S3 HeadBucket"
            ));
        }

        return {};
    }

    auto AwsS3Client::HeadObject(const std::string& key) -> Result<S3HeadObjectResult> {
        Aws::S3::Model::HeadObjectRequest request;
        request.SetBucket(m_impl->storage_config.bucket);
        request.SetKey(key);

        auto outcome = m_impl->client->HeadObject(request);
        if (!outcome.IsSuccess()) {
            if (IsNotFoundError(outcome.GetError())) {
                return S3HeadObjectResult{ .exists = false, .size = 0, .etag = {} };
            }
            return std::unexpected(ToErrorInfo(
                outcome.GetError(),
                ErrorCode::FileReadError,
                "S3 HeadObject"
            ));
        }

        return S3HeadObjectResult{
            .exists = true,
            .size = static_cast<uint64_t>(outcome.GetResult().GetContentLength()),
            .etag = outcome.GetResult().GetETag()
        };
    }

    auto AwsS3Client::PutObjectIfAbsent(const std::string& key, std::string data)
        -> Result<S3PutObjectResult> {
        auto input_data = Aws::MakeShared<Aws::StringStream>(AWS_ALLOC_TAG);
        input_data->write(data.data(), static_cast<std::streamsize>(data.size()));
        input_data->seekg(0);

        Aws::S3::Model::PutObjectRequest request;
        request.SetBucket(m_impl->storage_config.bucket);
        request.SetKey(key);
        request.SetContentLength(static_cast<long long>(data.size()));
        request.SetBody(input_data);
        request.SetIfNoneMatch("*");

        auto outcome = m_impl->client->PutObject(request);
        if (!outcome.IsSuccess()) {
            if (IsPreconditionFailedError(outcome.GetError())) {
                return S3PutObjectResult{ .etag = {}, .created = false };
            }
            return std::unexpected(ToErrorInfo(
                outcome.GetError(),
                ErrorCode::InternalError,
                "S3 PutObject"
            ));
        }

        return S3PutObjectResult{
            .etag = outcome.GetResult().GetETag(),
            .created = true,
        };
    }

    auto AwsS3Client::PutObjectFromFileIfAbsent(
        const std::string& key,
        const std::filesystem::path& local_path
    ) -> Result<S3PutObjectResult> {
        std::error_code size_error;
        const auto file_size = std::filesystem::file_size(local_path, size_error);
        if (size_error || file_size > static_cast<uintmax_t>(std::numeric_limits<long long>::max())) {
            return std::unexpected(
                ErrorInfo(ErrorCode::FileReadError, "Failed to inspect assembled file for S3 upload")
            );
        }

        auto input_data = Aws::MakeShared<Aws::FStream>(
            AWS_ALLOC_TAG,
            local_path.string().c_str(),
            std::ios_base::in | std::ios_base::binary
        );
        if (!input_data->good()) {
            return std::unexpected(
                ErrorInfo(ErrorCode::FileReadError, "Failed to open assembled file for S3 upload")
            );
        }

        Aws::S3::Model::PutObjectRequest request;
        request.SetBucket(m_impl->storage_config.bucket);
        request.SetKey(key);
        request.SetContentLength(static_cast<long long>(file_size));
        request.SetBody(input_data);
        request.SetIfNoneMatch("*");

        auto outcome = m_impl->client->PutObject(request);
        if (!outcome.IsSuccess()) {
            if (IsPreconditionFailedError(outcome.GetError())) {
                return S3PutObjectResult{ .etag = {}, .created = false };
            }
            return std::unexpected(ToErrorInfo(
                outcome.GetError(),
                ErrorCode::InternalError,
                "S3 PutObject"
            ));
        }

        return S3PutObjectResult{
            .etag = outcome.GetResult().GetETag(),
            .created = true,
        };
    }

    auto AwsS3Client::DeleteObject(const std::string& key) -> Result<void> {
        Aws::S3::Model::DeleteObjectRequest request;
        request.SetBucket(m_impl->storage_config.bucket);
        request.SetKey(key);

        auto outcome = m_impl->client->DeleteObject(request);
        if (!outcome.IsSuccess() && !IsNotFoundError(outcome.GetError())) {
            return std::unexpected(ToErrorInfo(
                outcome.GetError(),
                ErrorCode::InternalError,
                "S3 DeleteObject"
            ));
        }

        return {};
    }

    auto AwsS3Client::GetObjectRange(const std::string& key, uint64_t start, uint64_t length)
        -> Result<std::shared_ptr<StorageReadStream>> {
        Aws::S3::Model::GetObjectRequest request;
        request.SetBucket(m_impl->storage_config.bucket);
        request.SetKey(key);
        if (length > 0) {
            if (start > std::numeric_limits<uint64_t>::max() - (length - 1)) {
                return std::unexpected(
                    ErrorInfo(ErrorCode::ValidationFailed, "Invalid S3 object range")
                );
            }
            const auto end = start + length - 1;
            request.SetRange("bytes=" + std::to_string(start) + "-" + std::to_string(end));
        }

        auto outcome = m_impl->client->GetObject(request);
        if (!outcome.IsSuccess()) {
            if (IsNotFoundError(outcome.GetError())) {
                return std::unexpected(ErrorInfo(ErrorCode::FileNotFound, "S3 object not found"));
            }
            return std::unexpected(ToErrorInfo(
                outcome.GetError(),
                ErrorCode::FileReadError,
                "S3 GetObject"
            ));
        }

        return std::shared_ptr<StorageReadStream>(
            std::make_shared<S3ObjectReadStream>(std::move(outcome.GetResult()), length)
        );
    }

    auto AwsS3Client::ListObjects(
        const std::string& prefix,
        const std::string& continuation_token,
        uint32_t max_keys
    ) -> Result<S3ListObjectsResult> {
        if (max_keys == 0 || max_keys > MAX_DELETE_OBJECTS) {
            return std::unexpected(
                ErrorInfo(ErrorCode::ValidationFailed, "Invalid S3 list page size")
            );
        }

        Aws::S3::Model::ListObjectsV2Request request;
        request.SetBucket(m_impl->storage_config.bucket);
        request.SetPrefix(prefix);
        request.SetMaxKeys(static_cast<int>(max_keys));
        if (!continuation_token.empty()) {
            request.SetContinuationToken(continuation_token);
        }

        auto outcome = m_impl->client->ListObjectsV2(request);
        if (!outcome.IsSuccess()) {
            return std::unexpected(ToErrorInfo(
                outcome.GetError(),
                ErrorCode::FileReadError,
                "S3 ListObjectsV2"
            ));
        }

        S3ListObjectsResult result;
        result.keys.reserve(outcome.GetResult().GetContents().size());
        for (const auto& object : outcome.GetResult().GetContents()) {
            result.keys.emplace_back(object.GetKey());
        }
        result.is_truncated = outcome.GetResult().GetIsTruncated();
        result.continuation_token = outcome.GetResult().GetNextContinuationToken();
        return result;
    }

    auto AwsS3Client::DeleteObjects(const std::vector<std::string>& keys) -> Result<void> {
        if (keys.empty()) {
            return {};
        }
        if (keys.size() > MAX_DELETE_OBJECTS) {
            return std::unexpected(
                ErrorInfo(ErrorCode::ValidationFailed, "S3 batch delete exceeds 1000 objects")
            );
        }

        Aws::S3::Model::Delete delete_payload;
        delete_payload.SetQuiet(true);
        for (const auto& key : keys) {
            Aws::S3::Model::ObjectIdentifier object;
            object.SetKey(key);
            delete_payload.AddObjects(std::move(object));
        }

        Aws::S3::Model::DeleteObjectsRequest request;
        request.SetBucket(m_impl->storage_config.bucket);
        request.SetDelete(std::move(delete_payload));

        auto outcome = m_impl->client->DeleteObjects(request);
        if (!outcome.IsSuccess()) {
            return std::unexpected(ToErrorInfo(
                outcome.GetError(),
                ErrorCode::InternalError,
                "S3 DeleteObjects"
            ));
        }
        if (!outcome.GetResult().GetErrors().empty()) {
            const auto& error = outcome.GetResult().GetErrors().front();
            return std::unexpected(ErrorInfo(
                ErrorCode::InternalError,
                "S3 DeleteObjects partially failed: " + std::string(error.GetCode()) +
                    ": " + std::string(error.GetMessage())
            ));
        }
        return {};
    }

    auto AwsS3Client::CreateMultipartUpload(const std::string& key) -> Result<std::string> {
        Aws::S3::Model::CreateMultipartUploadRequest request;
        request.SetBucket(m_impl->storage_config.bucket);
        request.SetKey(key);

        auto outcome = m_impl->client->CreateMultipartUpload(request);
        if (!outcome.IsSuccess()) {
            return std::unexpected(ToErrorInfo(
                outcome.GetError(),
                ErrorCode::InternalError,
                "S3 CreateMultipartUpload"
            ));
        }
        if (outcome.GetResult().GetUploadId().empty()) {
            return std::unexpected(
                ErrorInfo(ErrorCode::InternalError, "S3 multipart upload ID is empty")
            );
        }
        return std::string(outcome.GetResult().GetUploadId());
    }

    auto AwsS3Client::UploadPart(
        const std::string& key,
        const std::string& upload_id,
        int part_number,
        std::string data
    ) -> Result<std::string> {
        auto part_validation = ValidatePartNumber(part_number);
        if (!part_validation) {
            return std::unexpected(part_validation.error());
        }
        if (upload_id.empty()) {
            return std::unexpected(
                ErrorInfo(ErrorCode::ValidationFailed, "S3 multipart upload ID is empty")
            );
        }

        auto input_data = Aws::MakeShared<Aws::StringStream>(AWS_ALLOC_TAG);
        input_data->write(data.data(), static_cast<std::streamsize>(data.size()));
        input_data->seekg(0);

        Aws::S3::Model::UploadPartRequest request;
        request.SetBucket(m_impl->storage_config.bucket);
        request.SetKey(key);
        request.SetUploadId(upload_id);
        request.SetPartNumber(part_number);
        request.SetContentLength(static_cast<long long>(data.size()));
        request.SetBody(input_data);

        auto outcome = m_impl->client->UploadPart(request);
        if (!outcome.IsSuccess()) {
            return std::unexpected(ToErrorInfo(
                outcome.GetError(),
                ErrorCode::InternalError,
                "S3 UploadPart"
            ));
        }
        if (outcome.GetResult().GetETag().empty()) {
            return std::unexpected(ErrorInfo(ErrorCode::InternalError, "S3 upload part ETag is empty"));
        }
        return std::string(outcome.GetResult().GetETag());
    }

    auto AwsS3Client::UploadPartCopy(
        const std::string& source_key,
        const std::string& destination_key,
        const std::string& upload_id,
        int part_number,
        uint64_t start,
        uint64_t length
    ) -> Result<std::string> {
        auto part_validation = ValidatePartNumber(part_number);
        if (!part_validation) {
            return std::unexpected(part_validation.error());
        }
        if (upload_id.empty() || length == 0 ||
            start > std::numeric_limits<uint64_t>::max() - (length - 1)) {
            return std::unexpected(
                ErrorInfo(ErrorCode::ValidationFailed, "Invalid S3 multipart copy request")
            );
        }

        const auto end = start + length - 1;
        Aws::S3::Model::UploadPartCopyRequest request;
        request.SetBucket(m_impl->storage_config.bucket);
        request.SetKey(destination_key);
        request.SetUploadId(upload_id);
        request.SetPartNumber(part_number);
        request.SetCopySource(m_impl->storage_config.bucket + "/" + source_key);
        request.SetCopySourceRange("bytes=" + std::to_string(start) + "-" + std::to_string(end));

        auto outcome = m_impl->client->UploadPartCopy(request);
        if (!outcome.IsSuccess()) {
            return std::unexpected(ToErrorInfo(
                outcome.GetError(),
                ErrorCode::InternalError,
                "S3 UploadPartCopy"
            ));
        }
        const auto& etag = outcome.GetResult().GetCopyPartResult().GetETag();
        if (etag.empty()) {
            return std::unexpected(ErrorInfo(ErrorCode::InternalError, "S3 copied part ETag is empty"));
        }
        return std::string(etag);
    }

    auto AwsS3Client::CompleteMultipartUpload(
        const std::string& key,
        const std::string& upload_id,
        const std::vector<S3CompletedPart>& parts,
        bool only_if_absent
    ) -> Result<S3CompleteMultipartResult> {
        if (upload_id.empty() || parts.empty()) {
            return std::unexpected(
                ErrorInfo(ErrorCode::ValidationFailed, "Invalid S3 multipart completion")
            );
        }

        Aws::S3::Model::CompletedMultipartUpload completed_upload;
        int previous_part_number = 0;
        for (const auto& part : parts) {
            auto part_validation = ValidatePartNumber(part.part_number);
            if (!part_validation || part.part_number <= previous_part_number || part.etag.empty()) {
                return std::unexpected(
                    ErrorInfo(ErrorCode::ValidationFailed, "Invalid S3 completed part list")
                );
            }

            Aws::S3::Model::CompletedPart completed_part;
            completed_part.SetPartNumber(part.part_number);
            completed_part.SetETag(part.etag);
            completed_upload.AddParts(std::move(completed_part));
            previous_part_number = part.part_number;
        }

        Aws::S3::Model::CompleteMultipartUploadRequest request;
        request.SetBucket(m_impl->storage_config.bucket);
        request.SetKey(key);
        request.SetUploadId(upload_id);
        request.SetMultipartUpload(std::move(completed_upload));
        if (only_if_absent) {
            request.SetIfNoneMatch("*");
        }

        auto outcome = m_impl->client->CompleteMultipartUpload(request);
        if (!outcome.IsSuccess()) {
            if (only_if_absent && IsPreconditionFailedError(outcome.GetError())) {
                return S3CompleteMultipartResult{ .created = false };
            }
            return std::unexpected(ToErrorInfo(
                outcome.GetError(),
                ErrorCode::InternalError,
                "S3 CompleteMultipartUpload"
            ));
        }
        return S3CompleteMultipartResult{ .created = true };
    }

    auto AwsS3Client::AbortMultipartUpload(const std::string& key, const std::string& upload_id)
        -> Result<void> {
        if (upload_id.empty()) {
            return std::unexpected(
                ErrorInfo(ErrorCode::ValidationFailed, "S3 multipart upload ID is empty")
            );
        }

        Aws::S3::Model::AbortMultipartUploadRequest request;
        request.SetBucket(m_impl->storage_config.bucket);
        request.SetKey(key);
        request.SetUploadId(upload_id);

        auto outcome = m_impl->client->AbortMultipartUpload(request);
        if (!outcome.IsSuccess() && !IsNotFoundError(outcome.GetError())) {
            return std::unexpected(ToErrorInfo(
                outcome.GetError(),
                ErrorCode::InternalError,
                "S3 AbortMultipartUpload"
            ));
        }
        return {};
    }

} // namespace disk::storage
