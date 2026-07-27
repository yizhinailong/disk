#include "storage/S3ObjectStorage.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <charconv>
#include <chrono>
#include <filesystem>
#include <functional>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <utility>
#include <vector>

#include <drogon/utils/coroutine.h>
#include <sodium/crypto_hash_sha256.h>
#include <trantor/net/EventLoop.h>
#include <trantor/utils/ConcurrentTaskQueue.h>

#include "services/MetricsService.hpp"
#include "storage/AssemblyConcurrencyLimiter.hpp"
#include "storage/StorageLogContext.hpp"
#include "utils/FileHashUtil.hpp"
#include "utils/LogHelper.hpp"

namespace disk::storage {

    namespace {
        constexpr int S3_DELETE_MAX_ATTEMPTS = 3;
        constexpr uint32_t S3_LIST_PAGE_SIZE = 1000;
        constexpr uint64_t S3_MULTIPART_PART_SIZE_BYTES = 5ULL * 1024 * 1024;
        constexpr int S3_MAX_MULTIPART_PARTS = 10000;
        constexpr size_t S3_MIN_READ_BUFFER_BYTES = 64 * 1024;
        constexpr size_t S3_MAX_READ_BUFFER_BYTES = 1024 * 1024;
        constexpr std::string_view S3_STORAGE_QUEUE_NAME = "s3-object-storage";

        [[nodiscard]] auto IsSafeObjectComponent(std::string_view value) -> bool {
            return !value.empty() && std::ranges::all_of(value, [](char character) {
                return (character >= 'a' && character <= 'z') ||
                       (character >= 'A' && character <= 'Z') ||
                       (character >= '0' && character <= '9') ||
                       character == '-' || character == '_';
            });
        }

        [[nodiscard]] auto IsLowerHex(std::string_view value, size_t expected_size) -> bool {
            return value.size() == expected_size &&
                   std::ranges::all_of(value, [](char character) {
                       return (character >= '0' && character <= '9') ||
                              (character >= 'a' && character <= 'f');
                   });
        }

        [[nodiscard]] auto IsSafeObjectPrefix(std::string_view prefix) -> bool {
            if (prefix.empty() || prefix.size() > 1024 || prefix.front() == '/' ||
                prefix.back() == '/' || prefix.contains('\\')) {
                return false;
            }

            size_t segment_start = 0;
            while (segment_start < prefix.size()) {
                const auto delimiter = prefix.find('/', segment_start);
                const auto segment_end = delimiter == std::string_view::npos ? prefix.size() : delimiter;
                const auto segment = prefix.substr(segment_start, segment_end - segment_start);
                if (segment.empty() || segment == "." || segment == ".." ||
                    !std::ranges::all_of(segment, [](char character) {
                        return (character >= 'a' && character <= 'z') ||
                               (character >= 'A' && character <= 'Z') ||
                               (character >= '0' && character <= '9') ||
                               character == '.' || character == '_' || character == '-';
                    })) {
                    return false;
                }
                if (delimiter == std::string_view::npos) {
                    break;
                }
                segment_start = delimiter + 1;
            }
            return true;
        }

        [[nodiscard]] auto ValidateS3Session(
            const UploadStagingSession& session,
            std::string_view configured_staging_prefix
        )
            -> Result<void> {
            const auto expected_prefix =
                std::string(configured_staging_prefix) + "/" + session.upload_id;
            if (session.backend != UploadStagingBackend::S3 ||
                !IsSafeObjectComponent(session.upload_id) ||
                !IsSafeObjectPrefix(session.prefix) ||
                session.prefix != expected_prefix) {
                return std::unexpected(
                    ErrorInfo(ErrorCode::ValidationFailed, "Invalid S3 upload staging session")
                );
            }
            return {};
        }

        [[nodiscard]] auto BuildS3ChunkKey(
            const UploadStagingSession& session,
            uint32_t chunk_index,
            std::string_view md5_hash
        ) -> std::string {
            return session.prefix + "/chunks/" + std::to_string(chunk_index) + "-" +
                   std::string(md5_hash) + ".part";
        }

        [[nodiscard]] auto BuildS3AssemblyKey(
            const UploadStagingSession& session,
            uint64_t state_version
        ) -> std::string {
            return session.prefix + "/assembled/" + std::to_string(state_version) + ".bin";
        }

        [[nodiscard]] auto IsAssemblyKeyForSession(
            const UploadStagingSession& session,
            std::string_view key
        ) -> bool {
            const auto prefix = session.prefix + "/assembled/";
            constexpr std::string_view suffix = ".bin";
            if (!key.starts_with(prefix) || !key.ends_with(suffix) ||
                key.size() <= prefix.size() + suffix.size()) {
                return false;
            }

            const auto version_text = key.substr(prefix.size(), key.size() - prefix.size() - suffix.size());
            uint64_t state_version = 0;
            const auto [end, error] = std::from_chars(
                version_text.data(),
                version_text.data() + version_text.size(),
                state_version
            );
            return error == std::errc{} && end == version_text.data() + version_text.size() &&
                   state_version > 0 && std::to_string(state_version) == version_text;
        }

        [[nodiscard]] auto ValidateS3AssemblyLocator(
            const UploadStagingAssembly& assembly,
            std::string_view configured_staging_prefix
        ) -> Result<std::string> {
            const auto session_root = std::string(configured_staging_prefix) + "/";
            if (assembly.backend != UploadStagingBackend::S3 ||
                !assembly.locator.starts_with(session_root)) {
                return std::unexpected(
                    ErrorInfo(ErrorCode::ValidationFailed, "Invalid S3 assembled object locator")
                );
            }

            const auto upload_id_start = session_root.size();
            const auto upload_id_end = assembly.locator.find('/', upload_id_start);
            if (upload_id_end == std::string::npos) {
                return std::unexpected(
                    ErrorInfo(ErrorCode::ValidationFailed, "Invalid S3 assembled object locator")
                );
            }

            const auto upload_id = assembly.locator.substr(
                upload_id_start,
                upload_id_end - upload_id_start
            );
            const UploadStagingSession session{
                .upload_id = upload_id,
                .backend = UploadStagingBackend::S3,
                .prefix = session_root + upload_id,
            };
            if (!ValidateS3Session(session, configured_staging_prefix) ||
                !IsAssemblyKeyForSession(session, assembly.locator)) {
                return std::unexpected(
                    ErrorInfo(ErrorCode::ValidationFailed, "Invalid S3 assembled object locator")
                );
            }
            return assembly.locator;
        }

        [[nodiscard]] auto ResolveFinalObjectKey(
            const std::filesystem::path& storage_path,
            std::string_view configured_object_prefix
        ) -> Result<std::string> {
            const auto key = storage_path.generic_string();
            const auto required_prefix = std::string(configured_object_prefix) + "/";
            if (!key.starts_with(required_prefix) || !IsSafeObjectPrefix(key)) {
                return std::unexpected(
                    ErrorInfo(ErrorCode::ValidationFailed, "Invalid S3 final object key")
                );
            }
            return key;
        }

        [[nodiscard]] auto VerifyFinalObjectSize(
            const std::shared_ptr<IS3Client>& client,
            const std::string& key,
            uint64_t expected_size,
            const disk::utils::LogContext& log_context
        ) -> Result<void> {
            auto head_result = client->HeadObject(key, log_context);
            if (!head_result) {
                return std::unexpected(head_result.error());
            }
            if (!head_result->exists || head_result->size != expected_size) {
                return std::unexpected(
                    ErrorInfo(ErrorCode::ChunkVerifyFailed, "S3 final object size mismatch")
                );
            }
            return {};
        }

        [[nodiscard]] auto ValidateS3ChunkDescriptor(
            const UploadStagingSession& session,
            size_t expected_position,
            const UploadStagingChunk& chunk
        ) -> Result<void> {
            if (chunk.chunk_index != expected_position || !IsLowerHex(chunk.md5_hash, 32) ||
                chunk.object_key != BuildS3ChunkKey(session, chunk.chunk_index, chunk.md5_hash) ||
                chunk.etag.empty()) {
                return std::unexpected(
                    ErrorInfo(ErrorCode::ChunkVerifyFailed, "Invalid S3 staging chunk descriptor")
                );
            }
            return {};
        }

        [[nodiscard]] auto ValidateS3ChunkIdentity(
            const UploadStagingSession& session,
            const UploadStagingChunk& chunk
        ) -> Result<void> {
            if (!IsLowerHex(chunk.md5_hash, 32) ||
                chunk.object_key != BuildS3ChunkKey(
                                        session,
                                        chunk.chunk_index,
                                        chunk.md5_hash
                                    )) {
                return std::unexpected(
                    ErrorInfo(ErrorCode::ChunkVerifyFailed, "Invalid S3 staging chunk identity")
                );
            }
            return {};
        }

        [[nodiscard]] auto ResolveS3ReadBufferSize(uint32_t configured_size) -> size_t {
            return std::clamp(
                static_cast<size_t>(configured_size),
                S3_MIN_READ_BUFFER_BYTES,
                S3_MAX_READ_BUFFER_BYTES
            );
        }

        [[nodiscard]] auto ValidateMultipartAbortDescriptor(
            const MultipartUploadDescriptor& descriptor,
            const disk::utils::S3StorageConfig& config
        ) -> Result<void> {
            const auto staging_prefix = config.staging_prefix + "/";
            const auto final_prefix = config.object_prefix + "/";
            if (descriptor.upload_id.empty() || descriptor.upload_id.size() > 4096 ||
                !IsSafeObjectPrefix(descriptor.key) ||
                (!descriptor.key.starts_with(staging_prefix) &&
                 !descriptor.key.starts_with(final_prefix))) {
                return std::unexpected(
                    ErrorInfo(ErrorCode::ValidationFailed, "Invalid S3 multipart abort descriptor")
                );
            }
            return {};
        }

        [[nodiscard]] auto ListS3Inventory(
            const std::shared_ptr<IS3Client>& client,
            const std::string& prefix,
            const std::string& continuation_token,
            size_t limit,
            const disk::utils::LogContext& log_context
        ) -> Result<StorageInventoryPage> {
            if (limit == 0 || limit > kMaxStorageInventoryPageSize || prefix.empty()) {
                return std::unexpected(
                    ErrorInfo(ErrorCode::ValidationFailed, "Invalid S3 inventory request")
                );
            }
            auto listed = client->ListObjects(
                prefix,
                continuation_token,
                static_cast<uint32_t>(limit),
                log_context
            );
            if (!listed) {
                return std::unexpected(listed.error());
            }
            if (!std::ranges::all_of(listed->keys, [&prefix](const auto& key) {
                    return key.starts_with(prefix);
                })) {
                return std::unexpected(
                    ErrorInfo(ErrorCode::InternalError, "S3 inventory returned an object outside its prefix")
                );
            }
            if (listed->is_truncated &&
                (listed->continuation_token.empty() ||
                 listed->continuation_token == continuation_token)) {
                return std::unexpected(
                    ErrorInfo(ErrorCode::InternalError, "S3 inventory pagination did not advance")
                );
            }

            StorageInventoryPage page;
            page.objects.reserve(listed->keys.size());
            for (const auto& key : listed->keys) {
                auto head = client->HeadObject(key, log_context);
                if (!head) {
                    return std::unexpected(head.error());
                }
                if (!head->exists) {
                    continue;
                }
                page.objects.push_back(StorageInventoryObject{
                    .locator = key,
                    .size_bytes = head->size,
                });
            }
            page.has_more = listed->is_truncated;
            if (page.has_more) {
                page.continuation_token = listed->continuation_token;
            }
            return page;
        }

        class MultipartAbortGuard final {
        public:
            MultipartAbortGuard(
                std::shared_ptr<IS3Client> client,
                std::shared_ptr<IMultipartUploadJournal> journal,
                MultipartUploadDescriptor descriptor,
                disk::utils::LogContext log_context
            ) : m_client(std::move(client)),
                m_journal(std::move(journal)),
                m_descriptor(std::move(descriptor)),
                m_log_context(std::move(log_context)) {}

            ~MultipartAbortGuard() {
                if (!m_active) {
                    return;
                }
                try {
                    auto abort_result = m_client->AbortMultipartUpload(
                        m_descriptor.key,
                        m_descriptor.upload_id,
                        m_log_context
                    );
                    if (abort_result) {
                        ResolveTracked("resolve aborted");
                        return;
                    }

                    Logger::Warn(m_log_context)
                        << "Failed to abort S3 multipart upload: error_code="
                        << abort_result.error().CodeInt();
                    if (m_tracked && m_journal != nullptr) {
                        auto release_result = m_journal->ReleaseForRetry(
                            m_descriptor,
                            abort_result.error().message
                        );
                        LogJournalFailure("release", release_result);
                    }
                } catch (const std::exception& error) {
                    static_cast<void>(error);
                    Logger::Warn(m_log_context)
                        << "S3 multipart abort cleanup threw";
                }
            }

            MultipartAbortGuard(const MultipartAbortGuard&) = delete;
            auto operator=(const MultipartAbortGuard&) -> MultipartAbortGuard& = delete;

            [[nodiscard]] auto Track() -> Result<void> {
                if (m_journal == nullptr) {
                    return {};
                }
                try {
                    auto result = m_journal->Track(m_descriptor);
                    m_tracked = result.has_value();
                    return result;
                } catch (const std::exception&) {
                    return std::unexpected(
                        ErrorInfo(ErrorCode::InternalError, "Failed to track multipart recovery task")
                    );
                }
            }

            [[nodiscard]] auto Renew() -> Result<void> {
                if (!m_tracked || m_journal == nullptr) {
                    return {};
                }
                try {
                    return m_journal->Renew(m_descriptor);
                } catch (const std::exception&) {
                    return std::unexpected(
                        ErrorInfo(ErrorCode::InternalError, "Failed to renew multipart recovery task")
                    );
                }
            }

            auto Release() noexcept -> void {
                m_active = false;
                ResolveTracked("resolve completed");
            }

        private:
            auto ResolveTracked(std::string_view action) noexcept -> void {
                if (!m_tracked || m_journal == nullptr) {
                    return;
                }
                try {
                    auto result = m_journal->Resolve(m_descriptor);
                    LogJournalFailure(action, result);
                } catch (const std::exception& error) {
                    static_cast<void>(error);
                    Logger::Warn(m_log_context)
                        << "Failed to " << action << " multipart recovery task";
                }
            }

            auto LogJournalFailure(std::string_view action, const Result<void>& result) const
                -> void {
                if (!result) {
                    Logger::Warn(m_log_context)
                        << "Failed to " << action
                        << " multipart recovery task: error_code="
                        << result.error().CodeInt();
                }
            }

            std::shared_ptr<IS3Client> m_client;
            std::shared_ptr<IMultipartUploadJournal> m_journal;
            MultipartUploadDescriptor m_descriptor;
            disk::utils::LogContext m_log_context;
            bool m_active{ true };
            bool m_tracked{ false };
        };

        [[nodiscard]] auto VerifyS3ChunkObject(
            const std::shared_ptr<IS3Client>& client,
            const UploadStagingChunk& chunk,
            size_t buffer_size,
            bool verify_etag,
            const disk::utils::LogContext& log_context
        ) -> Result<S3HeadObjectResult> {
            auto head_result = client->HeadObject(chunk.object_key, log_context);
            if (!head_result) {
                return std::unexpected(head_result.error());
            }
            if (!head_result->exists) {
                return std::unexpected(
                    ErrorInfo(ErrorCode::ChunkVerifyFailed, "S3 staging chunk object is missing")
                );
            }
            if (head_result->etag.empty()) {
                return std::unexpected(
                    ErrorInfo(ErrorCode::ChunkVerifyFailed, "S3 staging chunk ETag is empty")
                );
            }
            if (head_result->size != chunk.size_bytes ||
                (verify_etag && head_result->etag != chunk.etag)) {
                return std::unexpected(
                    ErrorInfo(ErrorCode::ChunkVerifyFailed, "S3 staging chunk metadata mismatch")
                );
            }

            auto stream_result = client->GetObjectRange(
                chunk.object_key,
                0,
                chunk.size_bytes,
                log_context
            );
            if (!stream_result) {
                return std::unexpected(stream_result.error());
            }
            if (stream_result.value() == nullptr) {
                return std::unexpected(
                    ErrorInfo(ErrorCode::FileReadError, "S3 staging chunk stream is unavailable")
                );
            }

            disk::utils::FileHashUtil::Md5Context md5_context{};
            disk::utils::FileHashUtil::Md5Init(md5_context);
            std::vector<char> buffer(buffer_size);
            uint64_t total_read = 0;
            while (total_read < chunk.size_bytes) {
                const auto remaining = chunk.size_bytes - total_read;
                const auto requested = static_cast<size_t>(std::min<uint64_t>(
                    remaining,
                    static_cast<uint64_t>(buffer.size())
                ));
                const auto bytes_read = stream_result.value()->Read(buffer.data(), requested);
                if (bytes_read == 0 || bytes_read > requested) {
                    stream_result.value()->Close();
                    return std::unexpected(
                        ErrorInfo(ErrorCode::ChunkVerifyFailed, "S3 staging chunk range is incomplete")
                    );
                }
                disk::utils::FileHashUtil::Md5Update(
                    md5_context,
                    std::bit_cast<const uint8_t*>(buffer.data()),
                    bytes_read
                );
                total_read += bytes_read;
            }
            stream_result.value()->Close();

            std::array<uint8_t, 16> digest{};
            disk::utils::FileHashUtil::Md5Final(md5_context, digest.data());
            if (disk::utils::FileHashUtil::BytesToHex(digest.data(), digest.size()) !=
                chunk.md5_hash) {
                return std::unexpected(
                    ErrorInfo(ErrorCode::ChunkVerifyFailed, "S3 staging chunk hash mismatch")
                );
            }
            return head_result.value();
        }

        [[nodiscard]] auto AssembleS3ChunksBlocking(
            const std::shared_ptr<IS3Client>& client,
            const std::shared_ptr<IMultipartUploadJournal>& journal,
            const UploadStagingSession& session,
            uint64_t state_version,
            const std::vector<UploadStagingChunk>& chunks,
            size_t read_buffer_size,
            const disk::utils::LogContext& log_context
        ) -> Result<UploadStagingAssembly> {
            if (journal == nullptr) {
                return std::unexpected(
                    ErrorInfo(ErrorCode::InternalError, "S3 multipart recovery journal is not configured")
                );
            }
            const auto assembly_key = BuildS3AssemblyKey(session, state_version);
            auto create_result = client->CreateMultipartUpload(assembly_key, log_context);
            if (!create_result) {
                return std::unexpected(create_result.error());
            }
            MultipartAbortGuard abort_guard(
                client,
                journal,
                MultipartUploadDescriptor{
                    .key = assembly_key,
                    .upload_id = create_result.value(),
                },
                log_context
            );
            auto track_result = abort_guard.Track();
            if (!track_result) {
                return std::unexpected(track_result.error());
            }

            disk::utils::FileHashUtil::Md5Context full_md5_context{};
            disk::utils::FileHashUtil::Md5Init(full_md5_context);
            crypto_hash_sha256_state full_sha256_context{};
            crypto_hash_sha256_init(&full_sha256_context);

            std::vector<char> read_buffer(read_buffer_size);
            std::string part_buffer;
            part_buffer.reserve(static_cast<size_t>(S3_MULTIPART_PART_SIZE_BYTES));
            std::vector<S3CompletedPart> completed_parts;
            uint64_t total_size_bytes = 0;
            int next_part_number = 1;

            const auto upload_part = [&]() -> Result<void> {
                if (part_buffer.empty()) {
                    return {};
                }
                if (next_part_number > S3_MAX_MULTIPART_PARTS) {
                    return std::unexpected(
                        ErrorInfo(ErrorCode::ValidationFailed, "S3 staging multipart exceeds 10000 parts")
                    );
                }

                auto renew_result = abort_guard.Renew();
                if (!renew_result) {
                    return std::unexpected(renew_result.error());
                }

                auto upload_result = client->UploadPart(
                    assembly_key,
                    create_result.value(),
                    next_part_number,
                    std::move(part_buffer),
                    log_context
                );
                if (!upload_result) {
                    return std::unexpected(upload_result.error());
                }
                completed_parts.push_back(S3CompletedPart{
                    .part_number = next_part_number,
                    .etag = std::move(upload_result.value()),
                });
                ++next_part_number;
                part_buffer = {};
                part_buffer.reserve(static_cast<size_t>(S3_MULTIPART_PART_SIZE_BYTES));
                return {};
            };

            for (const auto& chunk : chunks) {
                auto head_result = client->HeadObject(chunk.object_key, log_context);
                if (!head_result) {
                    return std::unexpected(head_result.error());
                }
                if (!head_result->exists || head_result->size != chunk.size_bytes ||
                    head_result->etag != chunk.etag) {
                    return std::unexpected(
                        ErrorInfo(ErrorCode::ChunkVerifyFailed, "S3 staging chunk metadata mismatch")
                    );
                }

                auto stream_result = client->GetObjectRange(
                    chunk.object_key,
                    0,
                    chunk.size_bytes,
                    log_context
                );
                if (!stream_result) {
                    return std::unexpected(stream_result.error());
                }
                if (stream_result.value() == nullptr) {
                    return std::unexpected(
                        ErrorInfo(ErrorCode::FileReadError, "S3 staging chunk stream is unavailable")
                    );
                }

                disk::utils::FileHashUtil::Md5Context chunk_md5_context{};
                disk::utils::FileHashUtil::Md5Init(chunk_md5_context);
                uint64_t chunk_size_bytes = 0;
                while (chunk_size_bytes < chunk.size_bytes) {
                    const auto chunk_remaining = chunk.size_bytes - chunk_size_bytes;
                    const auto part_remaining = S3_MULTIPART_PART_SIZE_BYTES - part_buffer.size();
                    const auto requested = static_cast<size_t>(std::min<uint64_t>(
                        std::min<uint64_t>(chunk_remaining, part_remaining),
                        static_cast<uint64_t>(read_buffer.size())
                    ));
                    const auto bytes_read = stream_result.value()->Read(read_buffer.data(), requested);
                    if (bytes_read == 0 || bytes_read > requested) {
                        stream_result.value()->Close();
                        return std::unexpected(
                            ErrorInfo(ErrorCode::ChunkVerifyFailed, "S3 staging chunk range is incomplete")
                        );
                    }

                    const auto* md5_bytes = std::bit_cast<const uint8_t*>(read_buffer.data());
                    const auto* sha256_bytes = std::bit_cast<const unsigned char*>(read_buffer.data());
                    disk::utils::FileHashUtil::Md5Update(
                        chunk_md5_context,
                        md5_bytes,
                        bytes_read
                    );
                    disk::utils::FileHashUtil::Md5Update(
                        full_md5_context,
                        md5_bytes,
                        bytes_read
                    );
                    crypto_hash_sha256_update(
                        &full_sha256_context,
                        sha256_bytes,
                        bytes_read
                    );
                    part_buffer.append(read_buffer.data(), bytes_read);
                    chunk_size_bytes += bytes_read;
                    if (total_size_bytes > std::numeric_limits<uint64_t>::max() - bytes_read) {
                        stream_result.value()->Close();
                        return std::unexpected(
                            ErrorInfo(ErrorCode::ValidationFailed, "S3 staging assembly size overflow")
                        );
                    }
                    total_size_bytes += bytes_read;

                    if (part_buffer.size() == S3_MULTIPART_PART_SIZE_BYTES) {
                        auto upload_result = upload_part();
                        if (!upload_result) {
                            stream_result.value()->Close();
                            return std::unexpected(upload_result.error());
                        }
                    }
                }
                stream_result.value()->Close();

                std::array<uint8_t, 16> chunk_digest{};
                disk::utils::FileHashUtil::Md5Final(
                    chunk_md5_context,
                    chunk_digest.data()
                );
                if (disk::utils::FileHashUtil::BytesToHex(
                        chunk_digest.data(),
                        chunk_digest.size()
                    ) != chunk.md5_hash) {
                    return std::unexpected(
                        ErrorInfo(ErrorCode::ChunkVerifyFailed, "S3 staging chunk hash mismatch")
                    );
                }
            }

            auto final_part_result = upload_part();
            if (!final_part_result) {
                return std::unexpected(final_part_result.error());
            }
            if (completed_parts.empty()) {
                return std::unexpected(
                    ErrorInfo(ErrorCode::ChunkVerifyFailed, "S3 staging assembly is empty")
                );
            }

            auto renew_result = abort_guard.Renew();
            if (!renew_result) {
                return std::unexpected(renew_result.error());
            }
            auto complete_result = client->CompleteMultipartUpload(
                assembly_key,
                create_result.value(),
                completed_parts,
                false,
                log_context
            );
            if (!complete_result) {
                return std::unexpected(complete_result.error());
            }
            abort_guard.Release();

            auto assembled_head = client->HeadObject(assembly_key, log_context);
            if (!assembled_head) {
                return std::unexpected(assembled_head.error());
            }
            if (!assembled_head->exists || assembled_head->size != total_size_bytes) {
                return std::unexpected(
                    ErrorInfo(ErrorCode::ChunkVerifyFailed, "S3 assembled object metadata mismatch")
                );
            }

            std::array<uint8_t, 16> md5_digest{};
            disk::utils::FileHashUtil::Md5Final(full_md5_context, md5_digest.data());
            std::array<uint8_t, crypto_hash_sha256_BYTES> sha256_digest{};
            crypto_hash_sha256_final(&full_sha256_context, sha256_digest.data());

            return UploadStagingAssembly{
                .backend = UploadStagingBackend::S3,
                .locator = assembly_key,
                .size_bytes = total_size_bytes,
                .md5_hash = disk::utils::FileHashUtil::BytesToHex(
                    md5_digest.data(),
                    md5_digest.size()
                ),
                .sha256_hash = disk::utils::FileHashUtil::BytesToHex(
                    sha256_digest.data(),
                    sha256_digest.size()
                ),
            };
        }

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
    } // namespace

    S3ObjectStorage::S3ObjectStorage(
        std::shared_ptr<disk::utils::ConfigMgr> config_mgr,
        std::shared_ptr<IS3Client> s3_client
    ) : m_config_mgr(config_mgr == nullptr ? disk::utils::ConfigMgr::GetInstance() : std::move(config_mgr)),
        m_s3_config(m_config_mgr->GetS3StorageConfig()),
        m_s3_client(std::move(s3_client)),
        m_local_staging(m_config_mgr),
        m_worker_queue(std::make_shared<trantor::ConcurrentTaskQueue>(m_s3_config.io_threads, std::string(S3_STORAGE_QUEUE_NAME))) {
        if (m_s3_client == nullptr) {
            throw std::runtime_error("S3ObjectStorage requires an S3 client");
        }
        disk::metrics::MetricsRegistry::GetInstance().RegisterThreadQueue(
            disk::metrics::ThreadQueue::S3,
            m_worker_queue,
            m_s3_config.io_threads
        );
        Logger::Info(StorageRuntimeLogContext()) << "S3 object storage initialized: max_connections="
                                                 << m_s3_config.max_connections
                                                 << ", io_threads=" << m_s3_config.io_threads;
    }

    auto S3ObjectStorage::SetMultipartUploadJournal(
        std::shared_ptr<IMultipartUploadJournal> journal
    ) -> void {
        if (journal == nullptr) {
            throw std::invalid_argument("S3 multipart upload journal cannot be null");
        }
        if (m_multipart_upload_journal != nullptr && m_multipart_upload_journal != journal) {
            throw std::logic_error("S3 multipart upload journal is already configured");
        }
        m_multipart_upload_journal = std::move(journal);
    }

    auto S3ObjectStorage::EnsureUploadSession(
        const UploadStagingSession& session,
        disk::utils::LogContext log_context
    )
        -> drogon::Task<Result<void>> {
        if (session.backend == UploadStagingBackend::Local) {
            co_return co_await m_local_staging.EnsureUploadSession(
                session,
                std::move(log_context)
            );
        }
        co_return ValidateS3Session(session, m_s3_config.staging_prefix);
    }

    auto S3ObjectStorage::WriteChunk(
        const UploadStagingSession& session,
        uint32_t chunk_index,
        const std::string& md5_hash,
        std::string data,
        disk::utils::LogContext log_context
    ) -> drogon::Task<Result<UploadStagingChunk>> {
        if (session.backend == UploadStagingBackend::Local) {
            co_return co_await m_local_staging.WriteChunk(
                session,
                chunk_index,
                md5_hash,
                std::move(data),
                std::move(log_context)
            );
        }
        auto session_validation = ValidateS3Session(session, m_s3_config.staging_prefix);
        if (!session_validation || !IsLowerHex(md5_hash, 32)) {
            co_return std::unexpected(
                session_validation ? ErrorInfo(ErrorCode::ChunkVerifyFailed, "Invalid S3 staging chunk hash") : session_validation.error()
            );
        }

        const auto key = BuildS3ChunkKey(session, chunk_index, md5_hash);
        const auto size_bytes = static_cast<uint64_t>(data.size());
        const auto read_buffer_size = ResolveS3ReadBufferSize(
            m_config_mgr->GetAssembleBufferSizeBytes()
        );
        auto client = m_s3_client;
        auto result = co_await RunBlockingS3Task(
            m_worker_queue,
            [client, key, chunk_index, md5_hash, size_bytes, read_buffer_size, data = std::move(data), log_context = std::move(log_context)]() mutable
                -> Result<UploadStagingChunk> {
                if (disk::utils::FileHashUtil::HashMd5(data) != md5_hash) {
                    return std::unexpected(
                        ErrorInfo(ErrorCode::ChunkVerifyFailed, "S3 staging chunk hash mismatch")
                    );
                }

                auto put_result = client->PutObjectIfAbsent(
                    key,
                    std::move(data),
                    log_context
                );
                if (!put_result) {
                    return std::unexpected(put_result.error());
                }

                UploadStagingChunk chunk{
                    .chunk_index = chunk_index,
                    .size_bytes = size_bytes,
                    .md5_hash = md5_hash,
                    .object_key = key,
                    .etag = put_result->etag,
                };
                if (!put_result->created) {
                    auto verify_result = VerifyS3ChunkObject(
                        client,
                        chunk,
                        read_buffer_size,
                        false,
                        log_context
                    );
                    if (!verify_result) {
                        return std::unexpected(verify_result.error());
                    }
                    chunk.etag = verify_result->etag;
                    return chunk;
                }

                auto head_result = client->HeadObject(key, log_context);
                if (!head_result) {
                    return std::unexpected(head_result.error());
                }
                if (!head_result->exists || head_result->size != size_bytes ||
                    (!put_result->etag.empty() && head_result->etag != put_result->etag)) {
                    return std::unexpected(
                        ErrorInfo(ErrorCode::ChunkVerifyFailed, "S3 staging chunk metadata mismatch")
                    );
                }
                chunk.etag = head_result->etag;
                if (chunk.etag.empty()) {
                    return std::unexpected(
                        ErrorInfo(ErrorCode::ChunkVerifyFailed, "S3 staging chunk ETag is empty")
                    );
                }
                return chunk;
            }
        );
        co_return result;
    }

    auto S3ObjectStorage::HeadChunkObject(
        const UploadStagingSession& session,
        const UploadStagingChunk& chunk,
        disk::utils::LogContext log_context
    ) -> drogon::Task<Result<UploadStagingObjectHead>> {
        if (session.backend == UploadStagingBackend::Local) {
            co_return co_await m_local_staging.HeadChunkObject(
                session,
                chunk,
                std::move(log_context)
            );
        }
        auto session_validation = ValidateS3Session(session, m_s3_config.staging_prefix);
        if (!session_validation) {
            co_return std::unexpected(session_validation.error());
        }
        auto chunk_validation = ValidateS3ChunkIdentity(session, chunk);
        if (!chunk_validation) {
            co_return std::unexpected(chunk_validation.error());
        }

        auto client = m_s3_client;
        auto result = co_await RunBlockingS3Task(
            m_worker_queue,
            [client, key = chunk.object_key, log_context = std::move(log_context)]()
                -> Result<UploadStagingObjectHead> {
                auto head = client->HeadObject(key, log_context);
                if (!head) {
                    return std::unexpected(head.error());
                }
                if (!head->exists) {
                    return UploadStagingObjectHead{};
                }
                return UploadStagingObjectHead{
                    .exists = true,
                    .size_bytes = head->size,
                    .etag = head->etag.empty() ? std::nullopt :
                                                 std::optional<std::string>(head->etag),
                };
            }
        );
        co_return result;
    }

    auto S3ObjectStorage::AssembleChunks(
        const UploadStagingSession& session,
        uint64_t state_version,
        uint32_t expected_chunk_count,
        const std::vector<UploadStagingChunk>& chunks,
        disk::utils::LogContext log_context
    )
        -> drogon::Task<Result<UploadStagingAssembly>> {
        if (session.backend == UploadStagingBackend::Local) {
            co_return co_await m_local_staging.AssembleChunks(
                session,
                state_version,
                expected_chunk_count,
                chunks,
                std::move(log_context)
            );
        }
        auto session_validation = ValidateS3Session(session, m_s3_config.staging_prefix);
        if (!session_validation) {
            co_return std::unexpected(session_validation.error());
        }
        if (state_version == 0 || chunks.size() != expected_chunk_count) {
            co_return std::unexpected(
                ErrorInfo(ErrorCode::ChunkVerifyFailed, "Upload chunk descriptor count mismatch")
            );
        }
        for (size_t position = 0; position < chunks.size(); ++position) {
            auto chunk_validation = ValidateS3ChunkDescriptor(session, position, chunks[position]);
            if (!chunk_validation) {
                co_return std::unexpected(chunk_validation.error());
            }
        }

        auto& limiter = AssemblyConcurrencyLimiter::GetInstance();
        auto slot_guard = limiter.TryAcquire();
        if (!slot_guard.has_value()) {
            co_return std::unexpected(ErrorInfo(
                ErrorCode::TooManyRequests,
                "Too many concurrent assembly operations, please retry later"
            ));
        }

        const auto read_buffer_size = ResolveS3ReadBufferSize(
            m_config_mgr->GetAssembleBufferSizeBytes()
        );
        auto client = m_s3_client;
        auto journal = m_multipart_upload_journal;
        auto result = co_await RunBlockingS3Task(
            m_worker_queue,
            [client, journal, session, state_version, chunks, read_buffer_size, log_context = std::move(log_context)]() {
                return AssembleS3ChunksBlocking(
                    client,
                    journal,
                    session,
                    state_version,
                    chunks,
                    read_buffer_size,
                    log_context
                );
            }
        );
        co_return result;
    }

    auto S3ObjectStorage::DiscardAssembly(
        const UploadStagingSession& session,
        const UploadStagingAssembly& assembly,
        disk::utils::LogContext log_context
    ) -> drogon::Task<Result<void>> {
        if (session.backend == UploadStagingBackend::Local) {
            co_return co_await m_local_staging.DiscardAssembly(
                session,
                assembly,
                std::move(log_context)
            );
        }
        auto session_validation = ValidateS3Session(session, m_s3_config.staging_prefix);
        if (!session_validation) {
            co_return std::unexpected(session_validation.error());
        }
        if (assembly.backend != UploadStagingBackend::S3 ||
            !IsAssemblyKeyForSession(session, assembly.locator)) {
            co_return std::unexpected(
                ErrorInfo(ErrorCode::ValidationFailed, "Invalid S3 staging assembly locator")
            );
        }

        auto client = m_s3_client;
        const auto key = assembly.locator;
        auto result = co_await RunBlockingS3Task(
            m_worker_queue,
            [client, key, log_context = std::move(log_context)]() {
                return client->DeleteObject(key, log_context);
            }
        );
        co_return result;
    }

    auto S3ObjectStorage::CleanupSession(
        const UploadStagingSession& session,
        disk::utils::LogContext log_context
    )
        -> drogon::Task<Result<void>> {
        if (session.backend == UploadStagingBackend::Local) {
            co_return co_await m_local_staging.CleanupSession(
                session,
                std::move(log_context)
            );
        }
        auto session_validation = ValidateS3Session(session, m_s3_config.staging_prefix);
        if (!session_validation) {
            co_return std::unexpected(session_validation.error());
        }

        const auto cleanup_prefix = session.prefix + "/";
        auto client = m_s3_client;
        auto result = co_await RunBlockingS3Task(
            m_worker_queue,
            [client, cleanup_prefix, log_context = std::move(log_context)]() -> Result<void> {
                std::string continuation_token;
                while (true) {
                    auto list_result = client->ListObjects(
                        cleanup_prefix,
                        continuation_token,
                        S3_LIST_PAGE_SIZE,
                        log_context
                    );
                    if (!list_result) {
                        return std::unexpected(list_result.error());
                    }
                    if (!std::ranges::all_of(list_result->keys, [&cleanup_prefix](const auto& key) {
                            return key.starts_with(cleanup_prefix);
                        })) {
                        return std::unexpected(
                            ErrorInfo(ErrorCode::InternalError, "S3 cleanup listed an object outside the upload session")
                        );
                    }
                    if (!list_result->keys.empty()) {
                        auto delete_result = client->DeleteObjects(
                            list_result->keys,
                            log_context
                        );
                        if (!delete_result) {
                            return std::unexpected(delete_result.error());
                        }
                    }
                    if (!list_result->is_truncated) {
                        return {};
                    }
                    if (list_result->continuation_token.empty() ||
                        list_result->continuation_token == continuation_token) {
                        return std::unexpected(
                            ErrorInfo(ErrorCode::InternalError, "S3 cleanup pagination did not advance")
                        );
                    }
                    continuation_token = std::move(list_result->continuation_token);
                }
            }
        );
        co_return result;
    }

    auto S3ObjectStorage::ListStagingObjects(
        const std::string& continuation_token,
        size_t limit,
        disk::utils::LogContext log_context
    ) -> drogon::Task<Result<StorageInventoryPage>> {
        const auto prefix = m_s3_config.staging_prefix + "/";
        auto client = m_s3_client;
        auto result = co_await RunBlockingS3Task(
            m_worker_queue,
            [client, prefix, continuation_token, limit, log_context = std::move(log_context)]() {
                return ListS3Inventory(
                    client,
                    prefix,
                    continuation_token,
                    limit,
                    log_context
                );
            }
        );
        co_return result;
    }

    auto S3ObjectStorage::PromoteToFinal(
        const UploadStagingAssembly& assembly,
        const std::string& sha256_hash,
        disk::utils::LogContext log_context
    ) -> drogon::Task<Result<BlobPromoteResult>> {
        if (!IsLowerHex(sha256_hash, 64) || assembly.sha256_hash != sha256_hash ||
            assembly.locator.empty()) {
            co_return std::unexpected(
                ErrorInfo(ErrorCode::ValidationFailed, "Invalid SHA-256 S3 blob promotion request")
            );
        }

        std::string source_key;
        if (assembly.backend == UploadStagingBackend::S3) {
            auto source_result = ValidateS3AssemblyLocator(assembly, m_s3_config.staging_prefix);
            if (!source_result) {
                co_return std::unexpected(source_result.error());
            }
            source_key = std::move(source_result.value());
        } else if (assembly.backend != UploadStagingBackend::Local) {
            co_return std::unexpected(
                ErrorInfo(ErrorCode::ValidationFailed, "Unsupported staging backend for S3 promotion")
            );
        }

        const auto local_source_path = std::filesystem::path(assembly.locator);
        const auto object_key = std::filesystem::path(m_s3_config.object_prefix) / "sha256" /
                                sha256_hash.substr(0, 2) / (sha256_hash + ".bin");
        const auto key = object_key.generic_string();
        const auto expected_size = assembly.size_bytes;
        auto client = m_s3_client;
        auto journal = m_multipart_upload_journal;

        auto result = co_await RunBlockingS3Task(
            m_worker_queue,
            [client, journal, key, source_key, local_source_path, object_key, expected_size, backend = assembly.backend, log_context = std::move(log_context)]()
                -> Result<BlobPromoteResult> {
                auto head_result = client->HeadObject(key, log_context);
                if (!head_result) {
                    return std::unexpected(head_result.error());
                }
                if (head_result->exists) {
                    if (head_result->size != expected_size) {
                        return std::unexpected(
                            ErrorInfo(ErrorCode::ChunkVerifyFailed, "Existing S3 final object size mismatch")
                        );
                    }
                    return BlobPromoteResult{ .path = object_key, .created = false };
                }

                if (backend == UploadStagingBackend::Local) {
                    std::error_code size_error;
                    const auto source_size = std::filesystem::file_size(local_source_path, size_error);
                    if (size_error || source_size != expected_size) {
                        return std::unexpected(
                            ErrorInfo(ErrorCode::ChunkVerifyFailed, "Local assembled blob size mismatch")
                        );
                    }

                    auto put_result = client->PutObjectFromFileIfAbsent(
                        key,
                        local_source_path,
                        log_context
                    );
                    if (!put_result) {
                        return std::unexpected(put_result.error());
                    }
                    auto verify_result = VerifyFinalObjectSize(
                        client,
                        key,
                        expected_size,
                        log_context
                    );
                    if (!verify_result) {
                        return std::unexpected(verify_result.error());
                    }
                    return BlobPromoteResult{
                        .path = object_key,
                        .created = put_result->created,
                    };
                }

                auto source_head = client->HeadObject(source_key, log_context);
                if (!source_head) {
                    return std::unexpected(source_head.error());
                }
                if (!source_head->exists || source_head->size != expected_size) {
                    return std::unexpected(
                        ErrorInfo(ErrorCode::ChunkVerifyFailed, "S3 assembled source size mismatch")
                    );
                }

                if (expected_size == 0) {
                    auto put_result = client->PutObjectIfAbsent(key, {}, log_context);
                    if (!put_result) {
                        return std::unexpected(put_result.error());
                    }
                    auto verify_result = VerifyFinalObjectSize(
                        client,
                        key,
                        expected_size,
                        log_context
                    );
                    if (!verify_result) {
                        return std::unexpected(verify_result.error());
                    }
                    return BlobPromoteResult{
                        .path = object_key,
                        .created = put_result->created,
                    };
                }

                const auto part_count =
                    1 + ((expected_size - 1) / S3_MULTIPART_PART_SIZE_BYTES);
                if (part_count > static_cast<uint64_t>(S3_MAX_MULTIPART_PARTS)) {
                    return std::unexpected(
                        ErrorInfo(ErrorCode::ValidationFailed, "S3 final copy exceeds multipart part limit")
                    );
                }
                if (journal == nullptr) {
                    return std::unexpected(
                        ErrorInfo(ErrorCode::InternalError, "S3 multipart recovery journal is not configured")
                    );
                }

                auto create_result = client->CreateMultipartUpload(key, log_context);
                if (!create_result) {
                    return std::unexpected(create_result.error());
                }
                MultipartAbortGuard abort_guard(
                    client,
                    journal,
                    MultipartUploadDescriptor{
                        .key = key,
                        .upload_id = create_result.value(),
                    },
                    log_context
                );
                auto track_result = abort_guard.Track();
                if (!track_result) {
                    return std::unexpected(track_result.error());
                }

                std::vector<S3CompletedPart> completed_parts;
                completed_parts.reserve(static_cast<size_t>(part_count));
                uint64_t offset = 0;
                for (uint64_t part_index = 0; part_index < part_count; ++part_index) {
                    auto renew_result = abort_guard.Renew();
                    if (!renew_result) {
                        return std::unexpected(renew_result.error());
                    }
                    const auto length = std::min(
                        S3_MULTIPART_PART_SIZE_BYTES,
                        expected_size - offset
                    );
                    const auto part_number = static_cast<int>(part_index + 1);
                    auto copy_result = client->UploadPartCopy(
                        source_key,
                        key,
                        create_result.value(),
                        part_number,
                        offset,
                        length,
                        log_context
                    );
                    if (!copy_result) {
                        return std::unexpected(copy_result.error());
                    }
                    completed_parts.push_back(S3CompletedPart{
                        .part_number = part_number,
                        .etag = std::move(copy_result.value()),
                    });
                    offset += length;
                }

                auto renew_result = abort_guard.Renew();
                if (!renew_result) {
                    return std::unexpected(renew_result.error());
                }
                auto complete_result = client->CompleteMultipartUpload(
                    key,
                    create_result.value(),
                    completed_parts,
                    true,
                    log_context
                );
                if (!complete_result) {
                    return std::unexpected(complete_result.error());
                }
                const bool created = complete_result->created;
                if (created) {
                    abort_guard.Release();
                }

                auto verify_result = VerifyFinalObjectSize(
                    client,
                    key,
                    expected_size,
                    log_context
                );
                if (!verify_result) {
                    return std::unexpected(verify_result.error());
                }

                return BlobPromoteResult{ .path = object_key, .created = created };
            }
        );

        co_return result;
    }

    auto S3ObjectStorage::OpenBlobRangeForRead(
        const BlobDescriptor& blob,
        uint64_t start,
        uint64_t length,
        disk::utils::LogContext log_context
    ) -> drogon::Task<Result<std::shared_ptr<StorageReadStream>>> {
        auto key_result = ResolveFinalObjectKey(
            std::filesystem::path(blob.storage_path),
            m_s3_config.object_prefix
        );
        if (!key_result) {
            co_return std::unexpected(key_result.error());
        }
        auto key = std::move(key_result.value());
        auto client = m_s3_client;
        auto result = co_await RunBlockingS3Task(
            m_worker_queue,
            [client, key, start, length, log_context = std::move(log_context)]()
                -> Result<std::shared_ptr<StorageReadStream>> {
                return client->GetObjectRange(key, start, length, log_context);
            }
        );
        co_return result;
    }

    auto S3ObjectStorage::DeleteBlob(
        const std::filesystem::path& storage_path,
        disk::utils::LogContext log_context
    )
        -> drogon::Task<Result<void>> {
        auto key_result = ResolveFinalObjectKey(storage_path, m_s3_config.object_prefix);
        if (!key_result) {
            co_return std::unexpected(key_result.error());
        }
        auto key = std::move(key_result.value());
        auto client = m_s3_client;
        auto result = co_await RunBlockingS3Task(
            m_worker_queue,
            [client, key, log_context = std::move(log_context)]() -> Result<void> {
                for (int attempt = 1; attempt <= S3_DELETE_MAX_ATTEMPTS; ++attempt) {
                    auto delete_result = client->DeleteObject(key, log_context);
                    if (delete_result) {
                        if (attempt > 1) {
                            Logger::Info(log_context)
                                << "S3 blob delete succeeded after retry: attempt=" << attempt
                                << "/" << S3_DELETE_MAX_ATTEMPTS;
                        }
                        return {};
                    }

                    Logger::Warn(log_context)
                        << "S3 blob delete attempt failed: attempt=" << attempt << "/"
                        << S3_DELETE_MAX_ATTEMPTS
                        << ", error_code=" << delete_result.error().CodeInt();
                    if (attempt == S3_DELETE_MAX_ATTEMPTS) {
                        return std::unexpected(delete_result.error());
                    }
                }

                return std::unexpected(
                    ErrorInfo(ErrorCode::InternalError, "S3 delete retry loop exhausted")
                );
            }
        );
        co_return result;
    }

    auto S3ObjectStorage::BlobExists(
        const BlobDescriptor& blob,
        disk::utils::LogContext log_context
    )
        -> drogon::Task<Result<bool>> {
        if (blob.storage_path.empty()) {
            co_return std::unexpected(
                ErrorInfo(ErrorCode::FileReadError, "Blob storage path is empty")
            );
        }
        auto key_result = ResolveFinalObjectKey(
            std::filesystem::path(blob.storage_path),
            m_s3_config.object_prefix
        );
        if (!key_result) {
            co_return std::unexpected(key_result.error());
        }
        auto key = std::move(key_result.value());
        auto client = m_s3_client;
        auto result = co_await RunBlockingS3Task(
            m_worker_queue,
            [client, key, log_context = std::move(log_context)]() -> Result<bool> {
                auto head_result = client->HeadObject(key, log_context);
                if (!head_result) {
                    return std::unexpected(head_result.error());
                }
                return head_result->exists;
            }
        );
        co_return result;
    }

    auto S3ObjectStorage::GetFileSize(
        const std::filesystem::path& storage_path,
        disk::utils::LogContext log_context
    )
        -> drogon::Task<Result<uint64_t>> {
        auto key_result = ResolveFinalObjectKey(storage_path, m_s3_config.object_prefix);
        if (!key_result) {
            co_return std::unexpected(key_result.error());
        }
        auto key = std::move(key_result.value());
        auto client = m_s3_client;
        auto result = co_await RunBlockingS3Task(
            m_worker_queue,
            [client, key, log_context = std::move(log_context)]() -> Result<uint64_t> {
                auto head_result = client->HeadObject(key, log_context);
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

    auto S3ObjectStorage::ListFinalObjects(
        const std::string& continuation_token,
        size_t limit,
        disk::utils::LogContext log_context
    ) -> drogon::Task<Result<StorageInventoryPage>> {
        const auto prefix = m_s3_config.object_prefix + "/";
        auto client = m_s3_client;
        auto result = co_await RunBlockingS3Task(
            m_worker_queue,
            [client, prefix, continuation_token, limit, log_context = std::move(log_context)]() {
                return ListS3Inventory(
                    client,
                    prefix,
                    continuation_token,
                    limit,
                    log_context
                );
            }
        );
        co_return result;
    }

    auto S3ObjectStorage::AbortMultipartUpload(
        const MultipartUploadDescriptor& descriptor,
        disk::utils::LogContext log_context
    ) -> drogon::Task<Result<void>> {
        auto validation = ValidateMultipartAbortDescriptor(descriptor, m_s3_config);
        if (!validation) {
            co_return std::unexpected(validation.error());
        }

        auto client = m_s3_client;
        auto result = co_await RunBlockingS3Task(
            m_worker_queue,
            [client, descriptor, log_context = std::move(log_context)]() {
                return client->AbortMultipartUpload(
                    descriptor.key,
                    descriptor.upload_id,
                    log_context
                );
            }
        );
        co_return result;
    }

} // namespace disk::storage
