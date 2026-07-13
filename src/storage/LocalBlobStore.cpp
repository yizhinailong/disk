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

#include "utils/ConfigMgr.hpp"
#include "utils/LogHelper.hpp"

namespace disk::storage {

    namespace {

        constexpr size_t MIN_LOCAL_BLOB_IO_THREADS = 4;
        constexpr size_t MAX_LOCAL_BLOB_IO_THREADS = 8;
        constexpr size_t DEFAULT_LOCAL_BLOB_IO_THREADS = 4;
        constexpr size_t DEFAULT_DELETE_TIMEOUT_SECONDS = 30;
        constexpr std::string_view LOCAL_BLOB_IO_QUEUE_NAME = "local-blob-store";

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
                            this->setValue(std::unexpected(
                                ErrorInfo(ErrorCode::InternalError, "Delete operation timed out")
                            ));
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

    } ///< namespace

    LocalBlobStore::LocalBlobStore(std::shared_ptr<disk::utils::ConfigMgr> config_mgr)
        : m_config_mgr(config_mgr == nullptr ? disk::utils::ConfigMgr::GetInstance() : std::move(config_mgr)) {
        const auto worker_thread_count = ResolveLocalBlobIoThreadCount(*m_config_mgr);
        m_worker_queue = std::make_shared<trantor::ConcurrentTaskQueue>(
            worker_thread_count,
            std::string(LOCAL_BLOB_IO_QUEUE_NAME)
        );

        Logger::Info() << "LocalBlobStore worker queue initialized: io_threads=" << worker_thread_count;
    }

    auto LocalBlobStore::PromoteToFinal(const std::filesystem::path& temp_path, const std::string& hash)
        -> drogon::Task<Result<BlobPromoteResult>> {
        const auto final_path = GetFinalStoragePath(hash);
        const auto final_dir = final_path.parent_path();

        auto result = co_await RunBlockingFilesystemTask(
            m_worker_queue,
            [temp_path, final_path, final_dir]() -> Result<BlobPromoteResult> {
                std::error_code ec;
                std::filesystem::create_directories(final_dir, ec);
                if (ec) {
                    return std::unexpected(
                        ErrorInfo(ErrorCode::InternalError, "Failed to create final storage directory")
                    );
                }

                if (std::filesystem::exists(final_path, ec)) {
                    std::filesystem::remove(temp_path, ec);
                    return BlobPromoteResult{ .path = final_path, .created = false };
                }
                ec.clear();

                std::filesystem::rename(temp_path, final_path, ec);
                if (!ec) {
                    return BlobPromoteResult{ .path = final_path, .created = true };
                }

                ec.clear();
                std::filesystem::copy_file(
                    temp_path,
                    final_path,
                    std::filesystem::copy_options::overwrite_existing,
                    ec
                );
                if (ec) {
                    return std::unexpected(
                        ErrorInfo(ErrorCode::InternalError, "Failed to move file to final storage path")
                    );
                }

                std::filesystem::remove(temp_path, ec);
                if (ec) {
                    return std::unexpected(
                        ErrorInfo(ErrorCode::InternalError, "Failed to cleanup temp file after copy")
                    );
                }

                return BlobPromoteResult{ .path = final_path, .created = true };
            }
        );

        co_return result;
    }

    auto LocalBlobStore::OpenForRead(const std::filesystem::path& storage_path)
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

    auto LocalBlobStore::DeleteBlob(const std::filesystem::path& storage_path)
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

    auto LocalBlobStore::Exists(const std::filesystem::path& storage_path)
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
        if (blob.hash_md5.empty()) {
            return std::nullopt;
        }
        return GetFinalStoragePath(blob.hash_md5);
    }

    auto LocalBlobStore::GetFinalStoragePath(const std::string& hash) const -> std::filesystem::path {
        return std::filesystem::path(m_config_mgr->GetStorageBasePath()) / hash.substr(0, 2) / (hash + ".bin");
    }

    auto LocalBlobStore::GetFileSize(const std::filesystem::path& storage_path)
        -> drogon::Task<Result<uint64_t>> {
        auto result = co_await RunBlockingFilesystemTask(
            m_worker_queue,
            [storage_path]() -> Result<uint64_t> {
                std::error_code ec;
                const auto file_size = std::filesystem::file_size(storage_path, ec);
                if (ec) {
                    return std::unexpected(
                        ErrorInfo(ErrorCode::FileReadError, "Failed to read blob size")
                    );
                }

                return file_size;
            }
        );

        co_return result;
    }

} ///< namespace disk::storage
