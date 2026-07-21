#include "storage/LocalFileStorage.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <fstream>
#include <functional>
#include <system_error>
#include <type_traits>
#include <utility>
#include <vector>

#include <drogon/utils/Utilities.h>
#include <drogon/utils/coroutine.h>
#include <sodium/crypto_hash_sha256.h>
#include <trantor/net/EventLoop.h>
#include <trantor/utils/ConcurrentTaskQueue.h>

#include "services/MetricsService.hpp"
#include "storage/AssemblyConcurrencyLimiter.hpp"
#include "utils/ConfigMgr.hpp"
#include "utils/FileHashUtil.hpp"
#include "utils/LogHelper.hpp"

namespace disk::storage {

    namespace {

        constexpr size_t MIN_LOCAL_FILE_IO_THREADS = 4;
        constexpr size_t MAX_LOCAL_FILE_IO_THREADS = 8;
        constexpr size_t DEFAULT_LOCAL_FILE_IO_THREADS = 4;
        constexpr size_t DEFAULT_DELETE_TIMEOUT_SECONDS = 30;
        constexpr size_t MIN_ASSEMBLY_IO_THREADS = 1;
        constexpr size_t MAX_ASSEMBLY_IO_THREADS = 4;
        constexpr std::string_view LOCAL_FILE_IO_QUEUE_NAME = "local-file-storage";
        constexpr std::string_view LOCAL_FILE_ASSEMBLY_QUEUE_NAME = "local-file-assembly";

        [[nodiscard]] auto IsSafeObjectComponent(std::string_view value) -> bool {
            return !value.empty() && std::ranges::all_of(value, [](char character) {
                return (character >= 'a' && character <= 'z') ||
                       (character >= 'A' && character <= 'Z') ||
                       (character >= '0' && character <= '9') ||
                       character == '-' || character == '_';
            });
        }

        [[nodiscard]] auto IsLowerHexMd5(std::string_view value) -> bool {
            return value.size() == 32 && std::ranges::all_of(value, [](char character) {
                       return (character >= '0' && character <= '9') ||
                              (character >= 'a' && character <= 'f');
                   });
        }

        [[nodiscard]] auto ValidateLocalSession(const UploadStagingSession& session)
            -> Result<void> {
            if (session.backend != UploadStagingBackend::Local ||
                !IsSafeObjectComponent(session.upload_id) || session.prefix.empty()) {
                return std::unexpected(
                    ErrorInfo(ErrorCode::ValidationFailed, "Invalid local upload staging session")
                );
            }
            return {};
        }

        auto ResolveConfiguredAssemblyConcurrency(const disk::utils::ConfigMgr& config_mgr) -> size_t {
            const auto configured_count =
                static_cast<size_t>(config_mgr.GetAssemblyMaxConcurrent());
            return configured_count == 0 ? AssemblyConcurrencyLimiter::DEFAULT_MAX_CONCURRENT : configured_count;
        }

        auto ResolveLocalFileIoThreadCount(const disk::utils::ConfigMgr& config_mgr) -> size_t {
            const auto configured = static_cast<size_t>(config_mgr.GetFileIoThreads());
            const auto resolved = configured == 0 ? DEFAULT_LOCAL_FILE_IO_THREADS : configured;
            return std::clamp(resolved, MIN_LOCAL_FILE_IO_THREADS, MAX_LOCAL_FILE_IO_THREADS);
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
                /// 先在线程池执行阻塞文件系统任务，再切回原协程继续处理结果。
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

                /// 某些单元测试可能不在事件循环线程内，此时直接恢复协程即可。
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
        auto& metrics = disk::metrics::MetricsRegistry::GetInstance();
        metrics.RegisterThreadQueue(
            disk::metrics::ThreadQueue::LocalFile,
            m_worker_queue,
            worker_thread_count
        );
        metrics.RegisterThreadQueue(
            disk::metrics::ThreadQueue::LocalAssembly,
            m_assembly_worker_queue,
            assembly_worker_thread_count
        );

        Logger::Info() << "LocalFileStorage worker queues initialized: io_threads=" << worker_thread_count
                       << ", assembly_threads=" << assembly_worker_thread_count;
    }

    auto LocalFileStorage::EnsureUploadSession(
        const UploadStagingSession& session,
        disk::utils::LogContext /*log_context*/
    )
        -> drogon::Task<Result<void>> {
        auto validation = ValidateLocalSession(session);
        if (!validation) {
            co_return std::unexpected(validation.error());
        }
        const auto& upload_id = session.upload_id;
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
        const UploadStagingSession& session,
        uint32_t chunk_index,
        const std::string& md5_hash,
        std::string data,
        disk::utils::LogContext /*log_context*/
    ) -> drogon::Task<Result<UploadStagingChunk>> {
        auto validation = ValidateLocalSession(session);
        if (!validation || !IsLowerHexMd5(md5_hash)) {
            co_return std::unexpected(
                ErrorInfo(ErrorCode::ValidationFailed, "Invalid upload staging object identity")
            );
        }
        const auto& upload_id = session.upload_id;

        const auto object_key = GetChunkObjectKey(upload_id, chunk_index, md5_hash);
        const auto chunk_path = std::filesystem::path(m_config_mgr->GetTempUploadPath()) / object_key;
        const auto chunk_dir = chunk_path.parent_path();
        const auto writing_path = std::filesystem::path(
            chunk_path.string() + ".writing-" + drogon::utils::getUuid()
        );
        const auto size_bytes = data.size();

        auto result = co_await RunBlockingFilesystemTask(
            m_worker_queue,
            [chunk_dir, chunk_path, writing_path, md5_hash, chunk_data = std::move(data)]() -> Result<void> {
                std::error_code ec;
                std::filesystem::create_directories(chunk_dir, ec);
                if (ec) {
                    return std::unexpected(
                        ErrorInfo(ErrorCode::InternalError, "Failed to create temp upload directory")
                    );
                }

                const auto VerifyExistingObject = [&chunk_path, &chunk_data, &md5_hash]() -> Result<void> {
                    std::error_code verify_ec;
                    const auto existing_size = std::filesystem::file_size(chunk_path, verify_ec);
                    if (verify_ec || existing_size != chunk_data.size()) {
                        return std::unexpected(
                            ErrorInfo(ErrorCode::ChunkVerifyFailed, "Staging chunk size mismatch")
                        );
                    }
                    auto existing_hash = FileHashUtil::HashFileMd5(chunk_path);
                    if (!existing_hash || existing_hash.value() != md5_hash) {
                        return std::unexpected(
                            ErrorInfo(ErrorCode::ChunkVerifyFailed, "Staging chunk hash mismatch")
                        );
                    }
                    return {};
                };

                const auto chunk_exists = std::filesystem::exists(chunk_path, ec);
                if (ec) {
                    return std::unexpected(
                        ErrorInfo(ErrorCode::InternalError, "Failed to inspect staging chunk")
                    );
                }
                if (chunk_exists) {
                    return VerifyExistingObject();
                }

                std::ofstream chunk_file(writing_path, std::ios::binary | std::ios::trunc);
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
                    std::filesystem::remove(writing_path, ec);
                    return std::unexpected(
                        ErrorInfo(ErrorCode::InternalError, "Failed to write chunk file")
                    );
                }

                std::filesystem::rename(writing_path, chunk_path, ec);
                if (!ec) {
                    return {};
                }

                ec.clear();
                if (std::filesystem::exists(chunk_path, ec) && !ec) {
                    auto verify_result = VerifyExistingObject();
                    std::filesystem::remove(writing_path, ec);
                    return verify_result;
                }

                std::filesystem::remove(writing_path, ec);
                return std::unexpected(
                    ErrorInfo(ErrorCode::InternalError, "Failed to publish immutable staging chunk")
                );
            }
        );

        if (!result) {
            co_return std::unexpected(result.error());
        }
        co_return UploadStagingChunk{ .chunk_index = chunk_index,
                                      .size_bytes = size_bytes,
                                      .md5_hash = md5_hash,
                                      .object_key = object_key,
                                      .etag = "" };
    }

    auto LocalFileStorage::HeadChunkObject(
        const UploadStagingSession& session,
        const UploadStagingChunk& chunk,
        disk::utils::LogContext /*log_context*/
    ) -> drogon::Task<Result<UploadStagingObjectHead>> {
        auto validation = ValidateLocalSession(session);
        if (!validation) {
            co_return std::unexpected(validation.error());
        }
        auto resolved_path = ResolveChunkFilePath(session.upload_id, chunk);
        if (!resolved_path) {
            co_return std::unexpected(resolved_path.error());
        }

        auto result = co_await RunBlockingFilesystemTask(
            m_worker_queue,
            [path = std::move(resolved_path.value())]() -> Result<UploadStagingObjectHead> {
                std::error_code error;
                const auto exists = std::filesystem::exists(path, error);
                if (error) {
                    return std::unexpected(
                        ErrorInfo(ErrorCode::FileReadError, "Failed to inspect staging chunk")
                    );
                }
                if (!exists) {
                    return UploadStagingObjectHead{};
                }

                const auto size_bytes = std::filesystem::file_size(path, error);
                if (error) {
                    return std::unexpected(
                        ErrorInfo(ErrorCode::FileReadError, "Failed to inspect staging chunk size")
                    );
                }
                return UploadStagingObjectHead{
                    .exists = true,
                    .size_bytes = static_cast<uint64_t>(size_bytes),
                };
            }
        );
        co_return result;
    }

    auto LocalFileStorage::AssembleChunks(
        const UploadStagingSession& session,
        uint64_t state_version,
        uint32_t expected_chunk_count,
        const std::vector<UploadStagingChunk>& chunks,
        disk::utils::LogContext log_context
    )
        -> drogon::Task<Result<UploadStagingAssembly>> {
        auto validation = ValidateLocalSession(session);
        if (!validation) {
            co_return std::unexpected(validation.error());
        }
        const auto& upload_id = session.upload_id;
        (void)state_version;
        auto start = std::chrono::steady_clock::now();
        auto& limiter = AssemblyConcurrencyLimiter::GetInstance();

        if (chunks.size() != expected_chunk_count) {
            co_return std::unexpected(
                ErrorInfo(ErrorCode::ChunkVerifyFailed, "Upload chunk descriptor count mismatch")
            );
        }

        std::vector<std::pair<UploadStagingChunk, std::filesystem::path>> resolved_chunks;
        resolved_chunks.reserve(chunks.size());
        for (size_t position = 0; position < chunks.size(); ++position) {
            const auto& chunk = chunks[position];
            if (chunk.chunk_index != position) {
                co_return std::unexpected(
                    ErrorInfo(ErrorCode::ChunkVerifyFailed, "Upload chunk indices are not contiguous")
                );
            }
            auto chunk_path = ResolveChunkFilePath(upload_id, chunk);
            if (!chunk_path) {
                co_return std::unexpected(chunk_path.error());
            }
            resolved_chunks.emplace_back(chunk, std::move(chunk_path.value()));
        }

        Logger::Debug(log_context) << "[assemble_chunks] start running_count=" << limiter.RunningCount()
                                   << " max_concurrent=" << limiter.MaxConcurrent()
                                   << " upload_id=" << upload_id;

        auto slot_guard = limiter.TryAcquire();
        if (!slot_guard.has_value()) {
            Logger::Warn(log_context) << "Assembly admission rejected: upload_id=" << upload_id
                                      << ", reason=local_capacity_exhausted"
                                      << ", running=" << limiter.RunningCount()
                                      << ", max_concurrent=" << limiter.MaxConcurrent();

            auto end = std::chrono::steady_clock::now();
            auto duration_us =
                std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
            Logger::Info(log_context) << "[assemble_chunks] duration_us=" << duration_us
                                      << " outcome=failure reason=local_capacity_exhausted"
                                      << " running_count=" << limiter.RunningCount()
                                      << " max_concurrent=" << limiter.MaxConcurrent();

            co_return std::unexpected(
                ErrorInfo(ErrorCode::TooManyRequests, "Too many concurrent assembly operations, please retry later")
            );
        }

        Logger::Debug(log_context) << "Assembly started: upload_id=" << upload_id << ", running=" << limiter.RunningCount()
                                   << ", max_concurrent=" << limiter.MaxConcurrent();

        const auto assembled_path = GetAssembleFilePath(upload_id);
        const auto assembled_parent = assembled_path.parent_path();
        const auto buffer_size = m_config_mgr->GetAssembleBufferSizeBytes();

        auto result = co_await RunBlockingFilesystemTask(
            m_assembly_worker_queue,
            [assembled_path, assembled_parent, resolved_chunks = std::move(resolved_chunks), buffer_size]() -> Result<UploadStagingAssembly> {
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
                uint64_t total_size_bytes = 0;
                for (const auto& [chunk, chunk_path] : resolved_chunks) {
                    std::ifstream chunk_file(chunk_path, std::ios::binary);
                    if (!chunk_file) {
                        assembled_file.close();
                        std::filesystem::remove(assembled_path, ec);
                        return std::unexpected(
                            ErrorInfo(ErrorCode::ChunkVerifyFailed, "Staging chunk object is missing")
                        );
                    }

                    FileHashUtil::Md5Context chunk_md5_ctx{};
                    FileHashUtil::Md5Init(chunk_md5_ctx);
                    uint64_t chunk_size_bytes = 0;

                    while (chunk_file.read(buffer.data(), static_cast<std::streamsize>(buffer.size()))) {
                        auto bytes_read = static_cast<size_t>(chunk_file.gcount());
                        const auto* md5_bytes = std::bit_cast<const uint8_t*>(buffer.data());
                        const auto* sha256_bytes = std::bit_cast<const unsigned char*>(buffer.data());
                        assembled_file.write(buffer.data(), static_cast<std::streamsize>(bytes_read));
                        FileHashUtil::Md5Update(md5_ctx, md5_bytes, bytes_read);
                        FileHashUtil::Md5Update(chunk_md5_ctx, md5_bytes, bytes_read);
                        crypto_hash_sha256_update(&sha256_state, sha256_bytes, bytes_read);
                        chunk_size_bytes += bytes_read;
                    }
                    if (chunk_file.gcount() > 0) {
                        auto bytes_read = static_cast<size_t>(chunk_file.gcount());
                        const auto* md5_bytes = std::bit_cast<const uint8_t*>(buffer.data());
                        const auto* sha256_bytes = std::bit_cast<const unsigned char*>(buffer.data());
                        assembled_file.write(buffer.data(), static_cast<std::streamsize>(bytes_read));
                        FileHashUtil::Md5Update(md5_ctx, md5_bytes, bytes_read);
                        FileHashUtil::Md5Update(chunk_md5_ctx, md5_bytes, bytes_read);
                        crypto_hash_sha256_update(&sha256_state, sha256_bytes, bytes_read);
                        chunk_size_bytes += bytes_read;
                    }
                    if (!chunk_file.eof()) {
                        assembled_file.close();
                        std::filesystem::remove(assembled_path, ec);
                        return std::unexpected(
                            ErrorInfo(ErrorCode::InternalError, "Failed to read chunk for assembling")
                        );
                    }

                    std::array<uint8_t, 16> chunk_md5_digest{};
                    FileHashUtil::Md5Final(chunk_md5_ctx, chunk_md5_digest.data());
                    const auto actual_chunk_md5 = FileHashUtil::BytesToHex(
                        chunk_md5_digest.data(),
                        chunk_md5_digest.size()
                    );
                    if ((chunk.size_bytes != 0 && chunk.size_bytes != chunk_size_bytes) ||
                        (!chunk.md5_hash.empty() && chunk.md5_hash != actual_chunk_md5)) {
                        assembled_file.close();
                        std::filesystem::remove(assembled_path, ec);
                        return std::unexpected(
                            ErrorInfo(ErrorCode::ChunkVerifyFailed, "Staging chunk metadata mismatch")
                        );
                    }
                    total_size_bytes += chunk_size_bytes;
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

                return UploadStagingAssembly{
                    .backend = UploadStagingBackend::Local,
                    .locator = assembled_path.string(),
                    .size_bytes = total_size_bytes,
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
            Logger::Info(log_context) << "[assemble_chunks] duration_us=" << duration_us
                                      << " outcome=failure running_count=" << limiter.RunningCount()
                                      << " max_concurrent=" << limiter.MaxConcurrent();
            co_return result;
        }

        Logger::Debug(log_context) << "Assembly completed: upload_id=" << upload_id << ", running=" << limiter.RunningCount()
                                   << ", max_concurrent=" << limiter.MaxConcurrent();

        auto end = std::chrono::steady_clock::now();
        auto duration_us =
            std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
        Logger::Debug(log_context) << "[assemble_chunks] duration_us=" << duration_us
                                   << " outcome=success running_count=" << limiter.RunningCount()
                                   << " max_concurrent=" << limiter.MaxConcurrent();

        co_return result;
    }

    auto LocalFileStorage::DeletePath(const std::filesystem::path& target_path)
        -> drogon::Task<Result<void>> {
        auto result = co_await RunBlockingFilesystemTaskWithTimeout(
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

    auto LocalFileStorage::DiscardAssembly(
        const UploadStagingSession& session,
        const UploadStagingAssembly& assembly,
        disk::utils::LogContext /*log_context*/
    ) -> drogon::Task<Result<void>> {
        auto validation = ValidateLocalSession(session);
        if (!validation) {
            co_return std::unexpected(validation.error());
        }
        const auto& upload_id = session.upload_id;
        const auto expected_path = GetAssembleFilePath(upload_id);
        if (assembly.backend != UploadStagingBackend::Local || assembly.locator.empty()) {
            co_return std::unexpected(
                ErrorInfo(ErrorCode::InternalError, "Local staging requires a local assembly object")
            );
        }
        const auto assembly_path = std::filesystem::path(assembly.locator);

        auto result = co_await RunBlockingFilesystemTask(
            m_worker_queue,
            [expected_path, assembly_path]() -> Result<void> {
                const auto normalized_expected = expected_path.lexically_normal();
                const auto normalized_assembly = assembly_path.lexically_normal();
                if (normalized_assembly != normalized_expected) {
                    return std::unexpected(
                        ErrorInfo(ErrorCode::InternalError, "Assembled temp file path mismatch")
                    );
                }

                std::error_code ec;
                const bool exists = std::filesystem::exists(expected_path, ec);
                if (ec) {
                    return std::unexpected(
                        ErrorInfo(ErrorCode::InternalError, "Failed to check assembled temp file")
                    );
                }
                if (!exists) {
                    return {};
                }

                std::filesystem::remove(expected_path, ec);
                if (ec) {
                    return std::unexpected(
                        ErrorInfo(ErrorCode::InternalError, "Failed to discard assembled temp file")
                    );
                }

                return {};
            }
        );

        co_return result;
    }

    auto LocalFileStorage::CleanupSession(
        const UploadStagingSession& session,
        disk::utils::LogContext /*log_context*/
    )
        -> drogon::Task<Result<void>> {
        auto validation = ValidateLocalSession(session);
        if (!validation) {
            co_return std::unexpected(validation.error());
        }
        const auto& upload_id = session.upload_id;
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

    auto LocalFileStorage::ListStagingObjects(
        const std::string& continuation_token,
        size_t limit,
        disk::utils::LogContext /*log_context*/
    ) -> drogon::Task<Result<StorageInventoryPage>> {
        const auto root = std::filesystem::path(m_config_mgr->GetTempUploadPath());
        auto result = co_await RunBlockingFilesystemTask(
            m_worker_queue,
            [root, continuation_token, limit]() {
                return ListLocalStorageInventory(
                    root,
                    continuation_token,
                    limit,
                    LocalInventoryLocator::RelativeToRoot
                );
            }
        );
        co_return result;
    }

    auto LocalFileStorage::GetTempDirPath(const std::string& upload_id) const -> std::filesystem::path {
        return std::filesystem::path(m_config_mgr->GetTempUploadPath()) / upload_id;
    }

    auto LocalFileStorage::GetChunkObjectKey(
        const std::string& upload_id,
        uint32_t chunk_index,
        const std::string& md5_hash
    ) -> std::string {
        return (std::filesystem::path(upload_id) / "chunks" /
                (std::to_string(chunk_index) + "-" + md5_hash + ".part"))
            .generic_string();
    }

    auto LocalFileStorage::ResolveChunkFilePath(
        const std::string& upload_id,
        const UploadStagingChunk& chunk
    ) const -> Result<std::filesystem::path> {
        if (!IsSafeObjectComponent(upload_id)) {
            return std::unexpected(
                ErrorInfo(ErrorCode::ValidationFailed, "Invalid upload staging object identity")
            );
        }

        if (chunk.object_key.empty()) {
            return GetTempDirPath(upload_id) / (std::to_string(chunk.chunk_index) + ".chunk");
        }
        if (!IsLowerHexMd5(chunk.md5_hash)) {
            return std::unexpected(
                ErrorInfo(ErrorCode::ChunkVerifyFailed, "Invalid staging chunk hash metadata")
            );
        }

        const auto expected_key = GetChunkObjectKey(upload_id, chunk.chunk_index, chunk.md5_hash);
        if (chunk.object_key != expected_key) {
            return std::unexpected(
                ErrorInfo(ErrorCode::ChunkVerifyFailed, "Staging chunk object key mismatch")
            );
        }
        return std::filesystem::path(m_config_mgr->GetTempUploadPath()) / expected_key;
    }

    auto LocalFileStorage::GetAssembleFilePath(const std::string& upload_id) const -> std::filesystem::path {
        return std::filesystem::path(m_config_mgr->GetTempUploadPath()) / (upload_id + ".tmp");
    }

} // namespace disk::storage
