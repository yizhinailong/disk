#include "storage/LocalBlobStore.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <functional>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <utility>

#include <drogon/utils/coroutine.h>
#include <trantor/net/EventLoop.h>
#include <trantor/utils/ConcurrentTaskQueue.h>

#include "services/MetricsService.hpp"
#include "storage/StorageLogContext.hpp"
#include "utils/ConfigMgr.hpp"
#include "utils/FileHashUtil.hpp"
#include "utils/LogHelper.hpp"

namespace disk::storage {

    namespace {

        constexpr size_t MIN_LOCAL_BLOB_IO_THREADS = 4;
        constexpr size_t MAX_LOCAL_BLOB_IO_THREADS = 8;
        constexpr size_t DEFAULT_LOCAL_BLOB_IO_THREADS = 4;
        constexpr size_t DEFAULT_DELETE_TIMEOUT_SECONDS = 30;
        constexpr std::string_view LOCAL_BLOB_IO_QUEUE_NAME = "local-blob-store";

        [[nodiscard]] auto IsSha256Hash(std::string_view value) -> bool {
            return value.size() == 64 && std::ranges::all_of(value, [](char character) {
                       return (character >= '0' && character <= '9') ||
                              (character >= 'a' && character <= 'f');
                   });
        }

        [[nodiscard]] auto VerifyExistingLocalBlob(
            const std::filesystem::path& final_path,
            uint64_t expected_size,
            std::string_view expected_sha256
        ) -> Result<void> {
            std::error_code ec;
            const auto size = std::filesystem::file_size(final_path, ec);
            if (ec || size != expected_size) {
                return std::unexpected(
                    ErrorInfo(ErrorCode::ChunkVerifyFailed, "Existing local final blob size mismatch")
                );
            }

            auto hash_result = disk::utils::FileHashUtil::HashFileSha256(final_path);
            if (!hash_result || hash_result.value() != expected_sha256) {
                return std::unexpected(
                    ErrorInfo(ErrorCode::ChunkVerifyFailed, "Existing local final blob hash mismatch")
                );
            }
            return {};
        }

        auto ResolveLocalBlobIoThreadCount(const disk::utils::ConfigMgr& config_mgr) -> size_t {
            const auto configured = static_cast<size_t>(config_mgr.GetFileIoThreads());
            const auto resolved = configured == 0 ? DEFAULT_LOCAL_BLOB_IO_THREADS : configured;
            return std::clamp(resolved, MIN_LOCAL_BLOB_IO_THREADS, MAX_LOCAL_BLOB_IO_THREADS);
        }

        template <typename T>
        class ConcurrentQueueAwaiter : public drogon::CallbackAwaiter<T> {
        public:
            ConcurrentQueueAwaiter(
                std::shared_ptr<trantor::ConcurrentTaskQueue> worker_queue,
                std::function<T()> task,
                trantor::EventLoop* resume_loop
            )
                : m_worker_queue(std::move(worker_queue)),
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
            trantor::EventLoop* m_resume_loop = nullptr;
        };

        template <typename Func>
        auto RunBlockingFilesystemTask(
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

        template <typename T>
        class ConcurrentQueueAwaiterWithTimeout : public drogon::CallbackAwaiter<T> {
        public:
            ConcurrentQueueAwaiterWithTimeout(
                std::shared_ptr<trantor::ConcurrentTaskQueue> worker_queue,
                std::function<T()> task,
                trantor::EventLoop* resume_loop,
                double timeout_seconds
            )
                : m_worker_queue(std::move(worker_queue)),
                  m_task(std::move(task)),
                  m_resume_loop(resume_loop),
                  m_timeout_seconds(timeout_seconds) {}

            auto await_suspend(std::coroutine_handle<> handle) -> void {
                m_handle = handle;

                m_worker_queue->runTaskInQueue([this]() mutable {
                    try {
                        auto result = m_task();
                        bool expected = false;
                        if (m_completed.compare_exchange_strong(expected, true)) {
                            this->setValue(std::move(result));
                            Resume();
                        }
                    } catch (...) {
                        bool expected = false;
                        if (m_completed.compare_exchange_strong(expected, true)) {
                            this->setException(std::current_exception());
                            Resume();
                        }
                    }
                });

                if (m_resume_loop != nullptr) {
                    m_resume_loop->runAfter(m_timeout_seconds, [this]() {
                        bool expected = false;
                        if (m_completed.compare_exchange_strong(expected, true)) {
                            this->setValue(std::unexpected(ErrorInfo(ErrorCode::InternalError, "Delete operation timed out")));
                            Resume();
                        }
                    });
                }
            }

        private:
            auto Resume() -> void {
                if (m_resume_loop != nullptr) {
                    m_resume_loop->queueInLoop([handle = m_handle]() mutable { handle.resume(); });
                    return;
                }
                m_handle.resume();
            }

            std::shared_ptr<trantor::ConcurrentTaskQueue> m_worker_queue;
            std::function<T()> m_task;
            trantor::EventLoop* m_resume_loop = nullptr;
            double m_timeout_seconds;
            std::atomic<bool> m_completed{ false };
            std::coroutine_handle<> m_handle;
        };

        template <typename Func>
        auto RunBlockingFilesystemTaskWithTimeout(
            std::shared_ptr<trantor::ConcurrentTaskQueue> worker_queue,
            Func&& task,
            double timeout_seconds = static_cast<double>(DEFAULT_DELETE_TIMEOUT_SECONDS)
        ) -> ConcurrentQueueAwaiterWithTimeout<std::decay_t<std::invoke_result_t<std::decay_t<Func>&>>> {
            using ResultType = std::decay_t<std::invoke_result_t<std::decay_t<Func>&>>;
            return ConcurrentQueueAwaiterWithTimeout<ResultType>(
                std::move(worker_queue),
                std::function<ResultType()>(std::forward<Func>(task)),
                trantor::EventLoop::getEventLoopOfCurrentThread(),
                timeout_seconds
            );
        }

    } // namespace

    LocalBlobStore::LocalBlobStore(std::shared_ptr<disk::utils::ConfigMgr> config_mgr)
        : m_config_mgr(config_mgr == nullptr ? disk::utils::ConfigMgr::GetInstance() : std::move(config_mgr)) {
        const auto worker_thread_count = ResolveLocalBlobIoThreadCount(*m_config_mgr);
        m_worker_queue = std::make_shared<trantor::ConcurrentTaskQueue>(
            worker_thread_count,
            std::string(LOCAL_BLOB_IO_QUEUE_NAME)
        );
        disk::metrics::MetricsRegistry::GetInstance().RegisterThreadQueue(
            disk::metrics::ThreadQueue::LocalBlob,
            m_worker_queue,
            worker_thread_count
        );

        Logger::Info(StorageRuntimeLogContext()) << "Local blob storage initialized: io_threads=" << worker_thread_count;
    }

    auto LocalBlobStore::PromoteToFinal(
        const UploadStagingAssembly& assembly,
        const std::string& sha256_hash,
        disk::utils::LogContext /*log_context*/
    )
        -> drogon::Task<Result<BlobPromoteResult>> {
        if (assembly.backend != UploadStagingBackend::Local || assembly.locator.empty()) {
            co_return std::unexpected(
                ErrorInfo(ErrorCode::InternalError, "Local blob store requires a local staging object")
            );
        }
        if (!IsSha256Hash(sha256_hash) || assembly.sha256_hash != sha256_hash) {
            co_return std::unexpected(
                ErrorInfo(ErrorCode::ValidationFailed, "Invalid SHA-256 hash for local blob promotion")
            );
        }

        const auto temp_path = std::filesystem::path(assembly.locator);
        const auto final_path = GetFinalStoragePath(sha256_hash);
        const auto final_dir = final_path.parent_path();
        const auto expected_size = assembly.size_bytes;

        auto result = co_await RunBlockingFilesystemTask(
            m_worker_queue,
            [temp_path, final_path, final_dir, expected_size, sha256_hash]() -> Result<BlobPromoteResult> {
                std::error_code ec;
                std::filesystem::create_directories(final_dir, ec);
                if (ec) {
                    return std::unexpected(
                        ErrorInfo(ErrorCode::InternalError, "Failed to create final storage directory")
                    );
                }

                if (std::filesystem::exists(final_path, ec)) {
                    auto verify_result = VerifyExistingLocalBlob(final_path, expected_size, sha256_hash);
                    if (!verify_result) {
                        return std::unexpected(verify_result.error());
                    }
                    return BlobPromoteResult{ .path = final_path, .created = false };
                }
                if (ec) {
                    return std::unexpected(
                        ErrorInfo(ErrorCode::InternalError, "Failed to inspect final storage path")
                    );
                }
                ec.clear();

                const auto source_size = std::filesystem::file_size(temp_path, ec);
                if (ec || source_size != expected_size) {
                    return std::unexpected(
                        ErrorInfo(ErrorCode::ChunkVerifyFailed, "Local assembled blob size mismatch")
                    );
                }
                ec.clear();

                const bool created = std::filesystem::copy_file(
                    temp_path,
                    final_path,
                    std::filesystem::copy_options::none,
                    ec
                );
                if (created && !ec) {
                    return BlobPromoteResult{ .path = final_path, .created = true };
                }

                if (ec == std::errc::file_exists || (!ec && std::filesystem::exists(final_path))) {
                    auto verify_result = VerifyExistingLocalBlob(final_path, expected_size, sha256_hash);
                    if (!verify_result) {
                        return std::unexpected(verify_result.error());
                    }
                    return BlobPromoteResult{ .path = final_path, .created = false };
                }

                if (ec) {
                    return std::unexpected(
                        ErrorInfo(ErrorCode::InternalError, "Failed to copy file to final storage path")
                    );
                }
                return std::unexpected(
                    ErrorInfo(ErrorCode::InternalError, "Final blob copy did not create an object")
                );
            }
        );

        co_return result;
    }

    auto LocalBlobStore::OpenForRead(
        const std::filesystem::path& storage_path,
        disk::utils::LogContext /*log_context*/
    )
        -> drogon::Task<Result<std::shared_ptr<std::ifstream>>> {
        auto result = co_await RunBlockingFilesystemTask(
            m_worker_queue,
            [storage_path]() -> Result<std::shared_ptr<std::ifstream>> {
                auto stream = std::make_shared<std::ifstream>(storage_path, std::ios::binary);
                if (!*stream) {
                    return std::unexpected(
                        ErrorInfo(ErrorCode::FileReadError, "Failed to open file for reading")
                    );
                }

                return stream;
            }
        );

        co_return result;
    }

    auto LocalBlobStore::DeleteBlob(
        const std::filesystem::path& storage_path,
        disk::utils::LogContext /*log_context*/
    )
        -> drogon::Task<Result<void>> {
        auto result = co_await RunBlockingFilesystemTaskWithTimeout(
            m_worker_queue,
            [storage_path]() -> Result<void> {
                std::error_code ec;
                const bool exists = std::filesystem::exists(storage_path, ec);
                if (ec) {
                    return std::unexpected(
                        ErrorInfo(ErrorCode::InternalError, "Failed to check blob path")
                    );
                }
                if (!exists) {
                    return {};
                }

                const bool is_directory = std::filesystem::is_directory(storage_path, ec);
                if (ec) {
                    return std::unexpected(
                        ErrorInfo(ErrorCode::InternalError, "Failed to inspect blob path")
                    );
                }

                if (is_directory) {
                    std::filesystem::remove_all(storage_path, ec);
                } else {
                    std::filesystem::remove(storage_path, ec);
                }

                if (ec) {
                    return std::unexpected(
                        ErrorInfo(ErrorCode::InternalError, "Failed to delete blob path")
                    );
                }

                return {};
            }
        );

        co_return result;
    }

    auto LocalBlobStore::Exists(
        const std::filesystem::path& storage_path,
        disk::utils::LogContext /*log_context*/
    )
        -> drogon::Task<Result<bool>> {
        auto result = co_await RunBlockingFilesystemTask(
            m_worker_queue,
            [storage_path]() -> Result<bool> {
                std::error_code ec;
                const bool exists = std::filesystem::exists(storage_path, ec);
                if (ec) {
                    return std::unexpected(
                        ErrorInfo(ErrorCode::InternalError, "Failed to check blob existence")
                    );
                }

                return exists;
            }
        );

        co_return result;
    }

    auto LocalBlobStore::GetLocalBlobPathForDownload(const BlobDescriptor& blob) const
        -> std::optional<std::filesystem::path> {
        if (blob.storage_path.empty()) {
            return std::nullopt;
        }
        return std::filesystem::path(blob.storage_path);
    }

    auto LocalBlobStore::GetFinalStoragePath(const std::string& sha256_hash) const
        -> std::filesystem::path {
        return std::filesystem::path(m_config_mgr->GetStorageBasePath()) / "sha256" /
               sha256_hash.substr(0, 2) / (sha256_hash + ".bin");
    }

    auto LocalBlobStore::GetFileSize(
        const std::filesystem::path& storage_path,
        disk::utils::LogContext /*log_context*/
    )
        -> drogon::Task<Result<uint64_t>> {
        auto result = co_await RunBlockingFilesystemTask(
            m_worker_queue,
            [storage_path]() -> Result<uint64_t> {
                std::error_code ec;
                const auto file_size = std::filesystem::file_size(storage_path, ec);
                if (ec) {
                    if (ec == std::errc::no_such_file_or_directory) {
                        return std::unexpected(
                            ErrorInfo(ErrorCode::FileNotFound, "Local blob not found")
                        );
                    }
                    return std::unexpected(
                        ErrorInfo(ErrorCode::FileReadError, "Failed to read blob size")
                    );
                }

                return file_size;
            }
        );

        co_return result;
    }

    auto LocalBlobStore::ListFinalObjects(
        const std::string& continuation_token,
        size_t limit,
        disk::utils::LogContext /*log_context*/
    ) -> drogon::Task<Result<StorageInventoryPage>> {
        const auto root = std::filesystem::path(m_config_mgr->GetStorageBasePath());
        auto result = co_await RunBlockingFilesystemTask(
            m_worker_queue,
            [root, continuation_token, limit]() {
                return ListLocalStorageInventory(
                    root,
                    continuation_token,
                    limit,
                    LocalInventoryLocator::IncludeRoot
                );
            }
        );
        co_return result;
    }

} // namespace disk::storage
