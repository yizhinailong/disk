#include "storage/S3ObjectStorage.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <functional>
#include <stdexcept>
#include <system_error>
#include <type_traits>
#include <utility>

#include <drogon/utils/coroutine.h>
#include <trantor/net/EventLoop.h>
#include <trantor/utils/ConcurrentTaskQueue.h>

#include "utils/LogHelper.hpp"

namespace disk::storage {

    namespace {
        constexpr size_t DEFAULT_S3_STORAGE_THREADS = 4;
        constexpr std::string_view S3_STORAGE_QUEUE_NAME = "s3-object-storage";

        template <typename T>
        class ConcurrentQueueAwaiter : public drogon::CallbackAwaiter<T> {
        public:
            ConcurrentQueueAwaiter(
                std::shared_ptr<trantor::ConcurrentTaskQueue> worker_queue,
                std::function<T()> task,
                trantor::EventLoop* resume_loop
            ) : m_worker_queue(std::move(worker_queue)),
                m_task(std::move(task)),
                m_resume_loop(resume_loop) {}

            auto await_suspend(std::coroutine_handle<> handle) -> void {
                m_worker_queue->runTaskInQueue([this, handle]() mutable {
                    try {
                        this->setValue(m_task());
                    } catch (...) {
                        this->setException(std::current_exception());
                    }
                    Resume(handle);
                });
            }

        private:
            auto Resume(std::coroutine_handle<> handle) -> void {
                if (m_resume_loop != nullptr) {
                    m_resume_loop->queueInLoop([handle]() mutable { handle.resume(); });
                    return;
                }
                handle.resume();
            }

            std::shared_ptr<trantor::ConcurrentTaskQueue> m_worker_queue;
            std::function<T()> m_task;
            trantor::EventLoop* m_resume_loop{ nullptr };
        };

        template <typename Func>
        auto RunBlockingS3Task(
            std::shared_ptr<trantor::ConcurrentTaskQueue> worker_queue,
            Func&& task
        ) -> ConcurrentQueueAwaiter<std::decay_t<std::invoke_result_t<std::decay_t<Func>&>>> {
            using ResultType = std::decay_t<std::invoke_result_t<std::decay_t<Func>&>>;
            return ConcurrentQueueAwaiter<ResultType>(
                std::move(worker_queue),
                std::function<ResultType()>(std::forward<Func>(task)),
                trantor::EventLoop::getEventLoopOfCurrentThread()
            );
        }
    } ///< namespace

    S3ObjectStorage::S3ObjectStorage(
        std::shared_ptr<disk::utils::ConfigMgr> config_mgr,
        std::shared_ptr<IS3Client> s3_client
    ) : m_config_mgr(config_mgr == nullptr ? disk::utils::ConfigMgr::GetInstance() : std::move(config_mgr)),
        m_s3_config(m_config_mgr->GetS3StorageConfig()),
        m_s3_client(std::move(s3_client)),
        m_local_staging(m_config_mgr),
        m_worker_queue(std::make_shared<trantor::ConcurrentTaskQueue>(
            DEFAULT_S3_STORAGE_THREADS,
            std::string(S3_STORAGE_QUEUE_NAME)
        )) {
        if (m_s3_client == nullptr) {
            throw std::runtime_error("S3ObjectStorage requires an S3 client");
        }
        Logger::Info() << "S3ObjectStorage initialized: bucket=" << m_s3_config.bucket
                       << ", prefix=" << m_s3_config.object_prefix;
    }

    auto S3ObjectStorage::EnsureUploadTempDir(const std::string& upload_id)
        -> drogon::Task<Result<void>> {
        co_return co_await m_local_staging.EnsureUploadTempDir(upload_id);
    }

    auto S3ObjectStorage::WriteChunk(
        const std::string& upload_id,
        uint32_t chunk_index,
        std::string data
    ) -> drogon::Task<Result<void>> {
        co_return co_await m_local_staging.WriteChunk(upload_id, chunk_index, std::move(data));
    }

    auto S3ObjectStorage::AssembleChunks(const std::string& upload_id, uint32_t chunk_count)
        -> drogon::Task<Result<UploadStagingAssembly>> {
        co_return co_await m_local_staging.AssembleChunks(upload_id, chunk_count);
    }

    auto S3ObjectStorage::DiscardAssembly(
        const std::string& upload_id,
        const UploadStagingAssembly& assembly
    ) -> drogon::Task<Result<void>> {
        co_return co_await m_local_staging.DiscardAssembly(upload_id, assembly);
    }

    auto S3ObjectStorage::CleanupTemp(const std::string& upload_id) -> drogon::Task<Result<void>> {
        co_return co_await m_local_staging.CleanupTemp(upload_id);
    }

    auto S3ObjectStorage::PromoteToFinal(const std::filesystem::path& temp_path, const std::string& hash)
        -> drogon::Task<Result<BlobPromoteResult>> {
        const auto object_key = GetFinalStoragePath(hash);
        const auto key = ToObjectKey(object_key);
        auto client = m_s3_client;

        auto result = co_await RunBlockingS3Task(
            m_worker_queue,
            [client, key, temp_path, object_key]() -> Result<BlobPromoteResult> {
                auto head_result = client->HeadObject(key);
                if (!head_result) {
                    return std::unexpected(head_result.error());
                }

                std::error_code ec;
                if (head_result->exists) {
                    std::filesystem::remove(temp_path, ec);
                    if (ec) {
                        Logger::Warn() << "Failed to remove reused assembled temp file after S3 dedup: "
                                       << temp_path << ", error=" << ec.message();
                    }
                    return BlobPromoteResult{ .path = object_key, .created = false };
                }

                auto put_result = client->PutObjectFromFile(key, temp_path);
                if (!put_result) {
                    return std::unexpected(put_result.error());
                }

                std::filesystem::remove(temp_path, ec);
                if (ec) {
                    Logger::Warn() << "Failed to remove assembled temp file after S3 upload: "
                                   << temp_path << ", error=" << ec.message();
                }

                return BlobPromoteResult{ .path = object_key, .created = true };
            }
        );

        co_return result;
    }

    auto S3ObjectStorage::OpenForRead(const std::filesystem::path& /*storage_path*/)
        -> drogon::Task<Result<std::shared_ptr<std::ifstream>>> {
        co_return std::unexpected(
            ErrorInfo(ErrorCode::FileReadError, "S3 storage does not support direct ifstream reads")
        );
    }

    auto S3ObjectStorage::OpenBlobRangeForRead(
        const BlobDescriptor& blob,
        uint64_t start,
        uint64_t length
    ) -> drogon::Task<Result<std::shared_ptr<StorageReadStream>>> {
        const auto key = ToObjectKey(GetFinalStoragePath(blob.hash_md5));
        auto client = m_s3_client;
        auto result = co_await RunBlockingS3Task(
            m_worker_queue,
            [client, key, start, length]() -> Result<std::shared_ptr<StorageReadStream>> {
                return client->GetObjectRange(key, start, length);
            }
        );
        co_return result;
    }

    auto S3ObjectStorage::DeleteBlob(const std::filesystem::path& storage_path)
        -> drogon::Task<Result<void>> {
        const auto key = ToObjectKey(storage_path);
        auto client = m_s3_client;
        auto result = co_await RunBlockingS3Task(
            m_worker_queue,
            [client, key]() -> Result<void> {
                return client->DeleteObject(key);
            }
        );
        co_return result;
    }

    auto S3ObjectStorage::Exists(const std::filesystem::path& storage_path)
        -> drogon::Task<Result<bool>> {
        const auto key = ToObjectKey(storage_path);
        auto client = m_s3_client;
        auto result = co_await RunBlockingS3Task(
            m_worker_queue,
            [client, key]() -> Result<bool> {
                auto head_result = client->HeadObject(key);
                if (!head_result) {
                    return std::unexpected(head_result.error());
                }
                return head_result->exists;
            }
        );
        co_return result;
    }

    auto S3ObjectStorage::GetFinalStoragePath(const std::string& hash) const -> std::filesystem::path {
        return std::filesystem::path(m_s3_config.object_prefix) / hash.substr(0, 2) / (hash + ".bin");
    }

    auto S3ObjectStorage::GetFileSize(const std::filesystem::path& storage_path)
        -> drogon::Task<Result<uint64_t>> {
        const auto key = ToObjectKey(storage_path);
        auto client = m_s3_client;
        auto result = co_await RunBlockingS3Task(
            m_worker_queue,
            [client, key]() -> Result<uint64_t> {
                auto head_result = client->HeadObject(key);
                if (!head_result) {
                    return std::unexpected(head_result.error());
                }
                if (!head_result->exists) {
                    return std::unexpected(ErrorInfo(ErrorCode::FileNotFound, "S3 object not found"));
                }
                return head_result->size;
            }
        );
        co_return result;
    }

    auto S3ObjectStorage::ToObjectKey(const std::filesystem::path& path) const -> std::string {
        return path.generic_string();
    }

} ///< namespace disk::storage
