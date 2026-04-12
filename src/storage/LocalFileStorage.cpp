#include "storage/LocalFileStorage.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <functional>
#include <system_error>
#include <type_traits>
#include <utility>
#include <vector>

#include <drogon/utils/coroutine.h>
#include <sodium/crypto_hash_sha256.h>
#include <trantor/net/EventLoop.h>
#include <trantor/utils/ConcurrentTaskQueue.h>
#include <trantor/utils/Logger.h>

#include "storage/AssemblyWorkerPool.hpp"
#include "utils/ConfigMgr.hpp"
#include "utils/FileHashUtil.hpp"

namespace disk::storage {

    namespace {

        constexpr size_t MIN_LOCAL_FILE_IO_THREADS = 2;
        constexpr size_t MAX_LOCAL_FILE_IO_THREADS = 4;
        constexpr size_t MIN_ASSEMBLY_IO_THREADS = 1;
        constexpr size_t MAX_ASSEMBLY_IO_THREADS = 4;
        constexpr std::string_view LOCAL_FILE_IO_QUEUE_NAME = "local-file-storage";
        constexpr std::string_view LOCAL_FILE_ASSEMBLY_QUEUE_NAME = "local-file-assembly";

        auto ResolveConfiguredAssemblyConcurrency(const disk::utils::ConfigMgr& config_mgr) -> size_t {
            const auto configured_count =
                static_cast<size_t>(config_mgr.GetAssemblyMaxConcurrent());
            return configured_count == 0 ? AssemblyWorkerPool::DEFAULT_MAX_CONCURRENT : configured_count;
        }

        auto ResolveLocalFileIoThreadCount(const disk::utils::ConfigMgr& config_mgr) -> size_t {
            return std::clamp(
                ResolveConfiguredAssemblyConcurrency(config_mgr),
                MIN_LOCAL_FILE_IO_THREADS,
                MAX_LOCAL_FILE_IO_THREADS
            );
        }

        auto ResolveAssemblyIoThreadCount(const disk::utils::ConfigMgr& config_mgr) -> size_t {
            return std::clamp(
                ResolveConfiguredAssemblyConcurrency(config_mgr),
                MIN_ASSEMBLY_IO_THREADS,
                MAX_ASSEMBLY_IO_THREADS
            );
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
                // 先在线程池执行阻塞文件系统任务，再切回原协程继续处理结果。
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

                // 某些单元测试可能不在事件循环线程内，此时直接恢复协程即可。
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

    } // namespace

    using disk::utils::FileHashUtil;

    LocalFileStorage::LocalFileStorage(std::shared_ptr<disk::utils::ConfigMgr> config_mgr)
        : m_config_mgr(config_mgr == nullptr ? disk::utils::ConfigMgr::GetInstance() : std::move(config_mgr)) {
        const auto worker_thread_count = ResolveLocalFileIoThreadCount(*m_config_mgr);
        const auto assembly_worker_thread_count = ResolveAssemblyIoThreadCount(*m_config_mgr);
        m_worker_queue = std::make_shared<trantor::ConcurrentTaskQueue>(
            worker_thread_count,
            std::string(LOCAL_FILE_IO_QUEUE_NAME)
        );
        m_assembly_worker_queue = std::make_shared<trantor::ConcurrentTaskQueue>(
            assembly_worker_thread_count,
            std::string(LOCAL_FILE_ASSEMBLY_QUEUE_NAME)
        );

        LOG_INFO << "LocalFileStorage worker queues initialized: io_threads=" << worker_thread_count
                 << ", assembly_threads=" << assembly_worker_thread_count;
    }

    auto LocalFileStorage::EnsureUploadTempDir(const std::string& upload_id)
        -> drogon::Task<Result<void>> {
        const auto temp_dir = GetTempDirPath(upload_id);

        auto result = co_await RunBlockingFilesystemTask(
            m_worker_queue,
            [temp_dir]() -> Result<void> {
                std::error_code ec;
                std::filesystem::create_directories(temp_dir, ec);
                if (ec) {
                    return std::unexpected(
                        ErrorInfo(ErrorCode::InternalError, "Failed to create temp upload directory")
                    );
                }

                return {};
            }
        );

        co_return result;
    }

    auto LocalFileStorage::WriteChunk(
        const std::string& upload_id,
        uint32_t chunk_index,
        std::string data
    ) -> drogon::Task<Result<void>> {
        const auto temp_dir = GetTempDirPath(upload_id);
        const auto chunk_path = GetChunkFilePath(upload_id, chunk_index);

        auto result = co_await RunBlockingFilesystemTask(
            m_worker_queue,
            [temp_dir, chunk_path, chunk_data = std::move(data)]() -> Result<void> {
                // 防御性回退：目录应由 EnsureUploadTempDir 预创建，此处仅处理意外丢失。
                std::error_code ec;
                if (!std::filesystem::exists(temp_dir, ec) || ec) {
                    std::filesystem::create_directories(temp_dir, ec);
                    if (ec) {
                        return std::unexpected(
                            ErrorInfo(ErrorCode::InternalError, "Failed to create temp upload directory")
                        );
                    }
                }

                std::ofstream chunk_file(chunk_path, std::ios::binary);
                if (!chunk_file) {
                    return std::unexpected(
                        ErrorInfo(ErrorCode::InternalError, "Failed to open chunk file")
                    );
                }

                chunk_file.write(
                    chunk_data.data(),
                    static_cast<std::streamsize>(chunk_data.size())
                );
                chunk_file.close();

                if (!chunk_file) {
                    return std::unexpected(
                        ErrorInfo(ErrorCode::InternalError, "Failed to write chunk file")
                    );
                }

                return {};
            }
        );

        co_return result;
    }

    auto LocalFileStorage::AssembleChunks(const std::string& upload_id, uint32_t chunk_count)
        -> drogon::Task<Result<AssembleResult>> {
        auto start = std::chrono::steady_clock::now();
        auto& pool = AssemblyWorkerPool::GetInstance();

        LOG_INFO << "[assemble_chunks] start active_count=" << pool.ActiveCount()
                 << " max_concurrent=" << pool.MaxConcurrent()
                 << " upload_id=" << upload_id;

        auto slot_guard = pool.TryAcquireGuard(upload_id);
        if (!slot_guard.has_value()) {
            const auto upload_already_active = pool.IsUploadActive(upload_id);
            const auto* message = upload_already_active ? "Upload assembly already in progress for this upload_id, please retry later" : "Too many concurrent assembly operations, please retry later";

            const auto* reason = upload_already_active ? "upload_already_active" : "pool_saturated";
            LOG_WARN << "Assembly admission rejected: upload_id=" << upload_id
                     << ", reason=" << reason
                     << ", running=" << pool.ActiveCount()
                     << ", max_concurrent=" << pool.MaxConcurrent();

            auto end = std::chrono::steady_clock::now();
            auto duration_us =
                std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
            LOG_INFO << "[assemble_chunks] duration_us=" << duration_us
                     << " outcome=failure reason=" << reason
                     << " active_count=" << pool.ActiveCount()
                     << " max_concurrent=" << pool.MaxConcurrent();

            co_return std::unexpected(
                ErrorInfo(ErrorCode::TooManyRequests, message)
            );
        }

        LOG_DEBUG << "Assembly started: upload_id=" << upload_id << ", running=" << pool.ActiveCount()
                  << ", max_concurrent=" << pool.MaxConcurrent();

        const auto temp_dir = GetTempDirPath(upload_id);
        const auto assembled_path = GetAssembleFilePath(upload_id);
        const auto assembled_parent = assembled_path.parent_path();
        const auto buffer_size = m_config_mgr->GetAssembleBufferSizeBytes();

        auto result = co_await RunBlockingFilesystemTask(
            m_assembly_worker_queue,
            [temp_dir, assembled_path, assembled_parent, chunk_count, buffer_size]() -> Result<AssembleResult> {
                std::error_code ec;
                std::filesystem::create_directories(assembled_parent, ec);
                if (ec) {
                    return std::unexpected(
                        ErrorInfo(ErrorCode::InternalError, "Failed to prepare assemble directory")
                    );
                }

                std::ofstream assembled_file(assembled_path, std::ios::binary);
                if (!assembled_file) {
                    return std::unexpected(
                        ErrorInfo(ErrorCode::InternalError, "Failed to create assembled temp file")
                    );
                }

                FileHashUtil::Md5Context md5_ctx{};
                FileHashUtil::Md5Init(md5_ctx);

                crypto_hash_sha256_state sha256_state;
                crypto_hash_sha256_init(&sha256_state);

                std::vector<char> buffer(buffer_size);
                for (uint32_t index = 0; index < chunk_count; ++index) {
                    const auto chunk_path = temp_dir / (std::to_string(index) + ".chunk");
                    std::ifstream chunk_file(chunk_path, std::ios::binary);
                    if (!chunk_file) {
                        assembled_file.close();
                        std::filesystem::remove(assembled_path, ec);
                        return std::unexpected(
                            ErrorInfo(ErrorCode::InternalError, "Failed to open chunk for assembling")
                        );
                    }

                    while (chunk_file.read(buffer.data(), static_cast<std::streamsize>(buffer.size()))) {
                        auto bytes_read = static_cast<size_t>(chunk_file.gcount());
                        const auto* md5_bytes = std::bit_cast<const uint8_t*>(buffer.data());
                        const auto* sha256_bytes = std::bit_cast<const unsigned char*>(buffer.data());
                        assembled_file.write(buffer.data(), static_cast<std::streamsize>(bytes_read));
                        FileHashUtil::Md5Update(md5_ctx, md5_bytes, bytes_read);
                        crypto_hash_sha256_update(&sha256_state, sha256_bytes, bytes_read);
                    }
                    if (chunk_file.gcount() > 0) {
                        auto bytes_read = static_cast<size_t>(chunk_file.gcount());
                        const auto* md5_bytes = std::bit_cast<const uint8_t*>(buffer.data());
                        const auto* sha256_bytes = std::bit_cast<const unsigned char*>(buffer.data());
                        assembled_file.write(buffer.data(), static_cast<std::streamsize>(bytes_read));
                        FileHashUtil::Md5Update(md5_ctx, md5_bytes, bytes_read);
                        crypto_hash_sha256_update(&sha256_state, sha256_bytes, bytes_read);
                    }
                    if (!chunk_file.eof()) {
                        assembled_file.close();
                        std::filesystem::remove(assembled_path, ec);
                        return std::unexpected(
                            ErrorInfo(ErrorCode::InternalError, "Failed to read chunk for assembling")
                        );
                    }
                }

                assembled_file.close();
                if (!assembled_file) {
                    std::filesystem::remove(assembled_path, ec);
                    return std::unexpected(
                        ErrorInfo(ErrorCode::InternalError, "Failed to write assembled temp file")
                    );
                }

                std::array<uint8_t, 16> md5_digest{};
                FileHashUtil::Md5Final(md5_ctx, md5_digest.data());

                std::array<uint8_t, crypto_hash_sha256_BYTES> sha256_digest{};
                crypto_hash_sha256_final(&sha256_state, sha256_digest.data());

                return AssembleResult{
                    .path = assembled_path,
                    .md5_hash = FileHashUtil::BytesToHex(md5_digest.data(), md5_digest.size()),
                    .sha256_hash =
                        FileHashUtil::BytesToHex(sha256_digest.data(), sha256_digest.size())
                };
            }
        );

        if (!result) {
            auto end = std::chrono::steady_clock::now();
            auto duration_us =
                std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
            LOG_INFO << "[assemble_chunks] duration_us=" << duration_us
                     << " outcome=failure active_count=" << pool.ActiveCount()
                     << " max_concurrent=" << pool.MaxConcurrent();
            co_return result;
        }

        LOG_DEBUG << "Assembly completed: upload_id=" << upload_id << ", running=" << pool.ActiveCount()
                  << ", max_concurrent=" << pool.MaxConcurrent();

        auto end = std::chrono::steady_clock::now();
        auto duration_us =
            std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
        LOG_INFO << "[assemble_chunks] duration_us=" << duration_us
                 << " outcome=success active_count=" << pool.ActiveCount()
                 << " max_concurrent=" << pool.MaxConcurrent();

        co_return result;
    }

    auto LocalFileStorage::PromoteToFinal(const std::filesystem::path& temp_path, const std::string& hash)
        -> drogon::Task<Result<std::filesystem::path>> {
        const auto final_path = GetFinalStoragePath(hash);
        const auto final_dir = final_path.parent_path();

        auto result = co_await RunBlockingFilesystemTask(
            m_worker_queue,
            [temp_path, final_path, final_dir]() -> Result<std::filesystem::path> {
                std::error_code ec;
                std::filesystem::create_directories(final_dir, ec);
                if (ec) {
                    return std::unexpected(
                        ErrorInfo(ErrorCode::InternalError, "Failed to create final storage directory")
                    );
                }

                if (std::filesystem::exists(final_path, ec)) {
                    std::filesystem::remove(temp_path, ec);
                    return final_path;
                }
                ec.clear();

                std::filesystem::rename(temp_path, final_path, ec);
                if (!ec) {
                    return final_path;
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

                return final_path;
            }
        );

        co_return result;
    }

    auto LocalFileStorage::OpenForRead(const std::filesystem::path& storage_path)
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

    auto LocalFileStorage::DeletePath(const std::filesystem::path& target_path)
        -> drogon::Task<Result<void>> {
        auto result = co_await RunBlockingFilesystemTask(
            m_worker_queue,
            [target_path]() -> Result<void> {
                std::error_code ec;
                const bool exists = std::filesystem::exists(target_path, ec);
                if (ec) {
                    return std::unexpected(
                        ErrorInfo(ErrorCode::InternalError, "Failed to check target path")
                    );
                }
                if (!exists) {
                    return {};
                }

                const bool is_directory = std::filesystem::is_directory(target_path, ec);
                if (ec) {
                    return std::unexpected(
                        ErrorInfo(ErrorCode::InternalError, "Failed to inspect target path")
                    );
                }

                if (is_directory) {
                    std::filesystem::remove_all(target_path, ec);
                } else {
                    std::filesystem::remove(target_path, ec);
                }

                if (ec) {
                    return std::unexpected(
                        ErrorInfo(ErrorCode::InternalError, "Failed to delete target path")
                    );
                }

                return {};
            }
        );

        co_return result;
    }

    auto LocalFileStorage::CleanupTemp(const std::string& upload_id) -> drogon::Task<Result<void>> {
        const auto temp_dir = GetTempDirPath(upload_id);
        const auto assembled_file = GetAssembleFilePath(upload_id);

        auto result = co_await RunBlockingFilesystemTask(
            m_worker_queue,
            [temp_dir, assembled_file]() -> Result<void> {
                std::error_code ec;
                std::filesystem::remove_all(temp_dir, ec);
                if (ec) {
                    return std::unexpected(
                        ErrorInfo(ErrorCode::InternalError, "Failed to cleanup temp directory")
                    );
                }

                std::filesystem::remove(assembled_file, ec);
                if (ec) {
                    return std::unexpected(
                        ErrorInfo(ErrorCode::InternalError, "Failed to cleanup assembled temp file")
                    );
                }

                return {};
            }
        );

        co_return result;
    }

    auto LocalFileStorage::Exists(const std::filesystem::path& target_path)
        -> drogon::Task<Result<bool>> {
        auto result = co_await RunBlockingFilesystemTask(
            m_worker_queue,
            [target_path]() -> Result<bool> {
                std::error_code ec;
                const bool exists = std::filesystem::exists(target_path, ec);
                if (ec) {
                    return std::unexpected(
                        ErrorInfo(ErrorCode::InternalError, "Failed to check path existence")
                    );
                }

                return exists;
            }
        );

        co_return result;
    }

    auto LocalFileStorage::GetFinalStoragePath(const std::string& hash) const -> std::filesystem::path {
        return std::filesystem::path(m_config_mgr->GetStorageBasePath()) / hash.substr(0, 2) / (hash + ".bin");
    }

    auto LocalFileStorage::GetFileSize(const std::filesystem::path& target_path)
        -> drogon::Task<Result<uint64_t>> {
        auto result = co_await RunBlockingFilesystemTask(
            m_worker_queue,
            [target_path]() -> Result<uint64_t> {
                std::error_code ec;
                const auto file_size = std::filesystem::file_size(target_path, ec);
                if (ec) {
                    return std::unexpected(
                        ErrorInfo(ErrorCode::FileReadError, "Failed to read file size")
                    );
                }

                return file_size;
            }
        );

        co_return result;
    }

    auto LocalFileStorage::GetTempDirPath(const std::string& upload_id) const -> std::filesystem::path {
        return std::filesystem::path(m_config_mgr->GetTempUploadPath()) / upload_id;
    }

    auto LocalFileStorage::GetChunkFilePath(const std::string& upload_id, uint32_t chunk_index) const
        -> std::filesystem::path {
        return GetTempDirPath(upload_id) / (std::to_string(chunk_index) + ".chunk");
    }

    auto LocalFileStorage::GetAssembleFilePath(const std::string& upload_id) const -> std::filesystem::path {
        return std::filesystem::path(m_config_mgr->GetTempUploadPath()) / (upload_id + ".tmp");
    }

} // namespace disk::storage
