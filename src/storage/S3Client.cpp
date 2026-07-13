#include "storage/S3Client.hpp"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <sstream>
#include <utility>

#include <aws/core/Aws.h>
#include <aws/core/auth/AWSCredentials.h>
#include <aws/core/auth/AWSCredentialsProvider.h>
#include <aws/core/client/ClientConfiguration.h>
#include <aws/core/http/HttpResponse.h>
#include <aws/core/utils/memory/stl/AWSStreamFwd.h>
#include <aws/s3/S3Client.h>
#include <aws/s3/model/DeleteObjectRequest.h>
#include <aws/s3/model/GetObjectRequest.h>
#include <aws/s3/model/HeadBucketRequest.h>
#include <aws/s3/model/HeadObjectRequest.h>
#include <aws/s3/model/PutObjectRequest.h>

#include "utils/LogHelper.hpp"

namespace disk::storage {

    namespace {
        constexpr const char* AWS_ALLOC_TAG = "disk-s3-storage";

        auto IsNotFoundError(const Aws::S3::S3Error& error) -> bool {
            const auto response_code = error.GetResponseCode();
            const auto exception_name = error.GetExceptionName();
            return response_code == Aws::Http::HttpResponseCode::NOT_FOUND ||
                   exception_name == "NoSuchKey" || exception_name == "NotFound";
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
    } ///< namespace

    class AwsS3Client::Impl {
    public:
        explicit Impl(disk::utils::S3StorageConfig config)
            : runtime(std::make_shared<AwsApiRuntime>()), storage_config(std::move(config)) {
            Aws::Client::ClientConfiguration client_config;
            client_config.region = storage_config.region;
            client_config.scheme = storage_config.use_ssl ? Aws::Http::Scheme::HTTPS : Aws::Http::Scheme::HTTP;
            client_config.verifySSL = storage_config.verify_ssl;
            client_config.connectTimeoutMs = storage_config.connect_timeout_ms;
            client_config.requestTimeoutMs = storage_config.request_timeout_ms;
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
                return S3HeadObjectResult{ .exists = false, .size = 0 };
            }
            return std::unexpected(ToErrorInfo(
                outcome.GetError(),
                ErrorCode::FileReadError,
                "S3 HeadObject"
            ));
        }

        return S3HeadObjectResult{
            .exists = true,
            .size = static_cast<uint64_t>(outcome.GetResult().GetContentLength())
        };
    }

    auto AwsS3Client::PutObjectFromFile(const std::string& key, const std::filesystem::path& local_path)
        -> Result<void> {
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
        request.SetBody(input_data);

        auto outcome = m_impl->client->PutObject(request);
        if (!outcome.IsSuccess()) {
            return std::unexpected(ToErrorInfo(
                outcome.GetError(),
                ErrorCode::InternalError,
                "S3 PutObject"
            ));
        }

        return {};
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

} ///< namespace disk::storage
