/**
 * @file ConfigMgr.cpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 配置管理类实现
 *
 * @copyright Copyright (c) 2026
 *
 */
#include "ConfigMgr.hpp"

#include <cstring>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <vector>

#include <drogon/utils/Utilities.h>

namespace disk::utils {
    namespace {
        auto ParseProcessRole(std::string_view value) -> ProcessRole {
            if (value == "api") {
                return ProcessRole::Api;
            }
            if (value == "worker") {
                return ProcessRole::Worker;
            }
            if (value == "all") {
                return ProcessRole::All;
            }
            throw std::runtime_error(
                "Invalid process_role: expected one of api, worker, all"
            );
        }

        auto ReadBoundedUInt(
            const Json::Value& config,
            const char* field,
            uint32_t fallback,
            uint32_t minimum,
            uint32_t maximum
        ) -> uint32_t {
            if (!config.isMember(field)) {
                return fallback;
            }

            const auto& value = config[field];
            if ((!value.isInt() && !value.isUInt()) || value.asInt64() < minimum ||
                value.asInt64() > maximum) {
                throw std::runtime_error(
                    std::string("Invalid ") + field + ": expected integer in range " +
                    std::to_string(minimum) + "-" + std::to_string(maximum)
                );
            }
            return static_cast<uint32_t>(value.asUInt());
        }

        auto ParseStorageBackend(std::string value) -> StorageBackend {
            if (value == "local") {
                return StorageBackend::Local;
            }
            if (value == "s3") {
                return StorageBackend::S3;
            }
            throw std::runtime_error("Invalid storage_backend: " + value);
        }

        auto NormalizeObjectPrefix(
            std::string prefix,
            std::string_view fallback,
            std::string_view field_name
        ) -> std::string {
            while (!prefix.empty() && prefix.front() == '/') {
                prefix.erase(prefix.begin());
            }
            while (!prefix.empty() && prefix.back() == '/') {
                prefix.pop_back();
            }
            if (prefix.empty()) {
                return std::string(fallback);
            }

            size_t segment_start = 0;
            for (size_t index = 0; index <= prefix.size(); ++index) {
                if (index < prefix.size() && prefix[index] != '/') {
                    const auto character = prefix[index];
                    const auto is_ascii_letter =
                        (character >= 'A' && character <= 'Z') ||
                        (character >= 'a' && character <= 'z');
                    const auto is_ascii_digit = character >= '0' && character <= '9';
                    if (!is_ascii_letter && !is_ascii_digit &&
                        character != '.' && character != '_' && character != '-') {
                        throw std::runtime_error(
                            "Invalid " + std::string(field_name) + ": unsupported character"
                        );
                    }
                    continue;
                }

                const auto segment = std::string_view(prefix).substr(segment_start, index - segment_start);
                if (segment.empty() || segment == "." || segment == "..") {
                    throw std::runtime_error(
                        "Invalid " + std::string(field_name) + ": unsafe path segment"
                    );
                }
                segment_start = index + 1;
            }

            return prefix;
        }

        auto PrefixesOverlap(std::string_view left, std::string_view right) -> bool {
            if (left == right) {
                return true;
            }
            return (left.size() < right.size() && right.starts_with(left) && right[left.size()] == '/') ||
                   (right.size() < left.size() && left.starts_with(right) && left[right.size()] == '/');
        }

        auto IsValidInstanceId(const std::string& instance_id) -> bool {
            constexpr size_t MAX_INSTANCE_ID_LENGTH = 128;
            if (instance_id.empty() || instance_id.size() > MAX_INSTANCE_ID_LENGTH) {
                return false;
            }

            for (const auto character : instance_id) {
                const auto is_ascii_letter =
                    (character >= 'A' && character <= 'Z') ||
                    (character >= 'a' && character <= 'z');
                const auto is_ascii_digit = character >= '0' && character <= '9';
                if (!is_ascii_letter && !is_ascii_digit &&
                    character != '.' && character != '_' && character != ':' && character != '-') {
                    return false;
                }
            }
            return true;
        }

        auto ValidateInstanceId(const std::string& instance_id, const char* source) -> void {
            if (!IsValidInstanceId(instance_id)) {
                throw std::runtime_error(
                    std::string("Invalid instance_id from ") + source +
                    ": expected 1-128 characters from [A-Za-z0-9._:-]"
                );
            }
        }
    } // namespace

    ConfigMgr::ConfigMgr()
        : m_generated_instance_id("disk-" + drogon::utils::getUuid()),
          m_instance_id(m_generated_instance_id) {}

    auto ConfigMgr::LoadConfig() -> void {
        const auto& custom_config = drogon::app().getCustomConfig();
        m_instance_id = m_generated_instance_id;
        m_process_role = ProcessRole::All;
        m_process_role_explicit = false;
        m_worker_poll_interval_ms = DEFAULT_WORKER_POLL_INTERVAL_MS;
        m_worker_claim_batch_size = DEFAULT_WORKER_CLAIM_BATCH_SIZE;
        m_worker_concurrency = DEFAULT_WORKER_CONCURRENCY;
        m_worker_lease_duration_seconds = DEFAULT_WORKER_LEASE_DURATION_SECONDS;
        m_worker_drain_timeout_seconds = DEFAULT_WORKER_DRAIN_TIMEOUT_SECONDS;
        m_upload_finalize_lease_seconds = DEFAULT_UPLOAD_FINALIZE_LEASE_SECONDS;
        m_storage_backend = StorageBackend::Local;
        m_upload_staging_backend = StorageBackend::Local;
        m_s3_storage_config = S3StorageConfig{};
        m_share_access_rate_limit_per_minute = DEFAULT_SHARE_ACCESS_RATE_LIMIT_PER_MINUTE;
        m_share_access_rate_limit_window_seconds = DEFAULT_SHARE_ACCESS_RATE_LIMIT_WINDOW_SECONDS;
        m_share_browse_rate_limit_per_minute = DEFAULT_SHARE_BROWSE_RATE_LIMIT_PER_MINUTE;
        m_share_browse_rate_limit_window_seconds = DEFAULT_SHARE_BROWSE_RATE_LIMIT_WINDOW_SECONDS;
        m_share_download_rate_limit_per_minute = DEFAULT_SHARE_DOWNLOAD_RATE_LIMIT_PER_MINUTE;
        m_share_download_rate_limit_window_seconds = DEFAULT_SHARE_DOWNLOAD_RATE_LIMIT_WINDOW_SECONDS;

        if (custom_config.isMember("disk")) {
            const auto& app_config = custom_config["disk"];

            if (app_config.isMember("process_role")) {
                if (!app_config["process_role"].isString()) {
                    throw std::runtime_error(
                        "Invalid process_role from custom_config.disk: expected string"
                    );
                }
                m_process_role = ParseProcessRole(app_config["process_role"].asString());
                m_process_role_explicit = true;
            }

            if (app_config.isMember("instance_id")) {
                if (!app_config["instance_id"].isString()) {
                    throw std::runtime_error("Invalid instance_id from custom_config.disk: expected string");
                }
                m_instance_id = app_config["instance_id"].asString();
                ValidateInstanceId(m_instance_id, "custom_config.disk");
            }

            if (app_config.isMember("upload_finalize_lease_seconds")) {
                const auto& lease_seconds = app_config["upload_finalize_lease_seconds"];
                if ((!lease_seconds.isInt() && !lease_seconds.isUInt()) ||
                    lease_seconds.asInt64() < 30 || lease_seconds.asInt64() > 3600) {
                    throw std::runtime_error(
                        "Invalid upload_finalize_lease_seconds: expected integer in range 30-3600"
                    );
                }
                m_upload_finalize_lease_seconds = static_cast<uint32_t>(lease_seconds.asUInt());
            }

            m_worker_poll_interval_ms = ReadBoundedUInt(
                app_config,
                "worker_poll_interval_ms",
                DEFAULT_WORKER_POLL_INTERVAL_MS,
                100,
                60000
            );
            m_worker_claim_batch_size = ReadBoundedUInt(
                app_config,
                "worker_claim_batch_size",
                DEFAULT_WORKER_CLAIM_BATCH_SIZE,
                1,
                1000
            );
            m_worker_concurrency = ReadBoundedUInt(
                app_config,
                "worker_concurrency",
                DEFAULT_WORKER_CONCURRENCY,
                1,
                1
            );
            m_worker_lease_duration_seconds = ReadBoundedUInt(
                app_config,
                "worker_lease_duration_seconds",
                DEFAULT_WORKER_LEASE_DURATION_SECONDS,
                30,
                3600
            );
            m_worker_drain_timeout_seconds = ReadBoundedUInt(
                app_config,
                "worker_drain_timeout_seconds",
                DEFAULT_WORKER_DRAIN_TIMEOUT_SECONDS,
                1,
                300
            );

            /// 从配置读取 storage_base_path
            if (app_config.isMember("storage_base_path")) {
                m_storage_base_path = app_config["storage_base_path"].asString();
                Logger::Info() << "Loaded storage_base_path from config: " << m_storage_base_path;
            } else {
                Logger::Warn() << "storage_base_path not found in config, using default: " << m_storage_base_path;
            }

            /// 从配置读取 temp_upload_path
            if (app_config.isMember("temp_upload_path")) {
                m_temp_upload_path = app_config["temp_upload_path"].asString();
                Logger::Info() << "Loaded temp_upload_path from config: " << m_temp_upload_path;
            } else {
                Logger::Warn() << "temp_upload_path not found in config, using default: " << m_temp_upload_path;
            }

            /// 从配置读取 chunk_size
            if (app_config.isMember("chunk_size")) {
                m_chunk_size = static_cast<uint32_t>(app_config["chunk_size"].asUInt());
                Logger::Info() << "Loaded chunk_size from config: " << m_chunk_size;
            }

            /// 从配置读取 max_file_size
            if (app_config.isMember("max_file_size")) {
                m_max_file_size = static_cast<uint64_t>(app_config["max_file_size"].asUInt64());
                Logger::Info() << "Loaded max_file_size from config: " << m_max_file_size;
            }

            /// 从配置读取 upload_task_expiry_seconds
            if (app_config.isMember("upload_task_expiry_seconds")) {
                m_upload_task_expiry_seconds = app_config["upload_task_expiry_seconds"].asInt();
                Logger::Info() << "Loaded upload_task_expiry_seconds from config: " << m_upload_task_expiry_seconds;
            }

            /// 从配置读取 assembly_max_concurrent
            if (app_config.isMember("assembly_max_concurrent")) {
                m_assembly_max_concurrent = static_cast<uint32_t>(app_config["assembly_max_concurrent"].asUInt());
                Logger::Info() << "Loaded assembly_max_concurrent from config: " << m_assembly_max_concurrent;
            }

            /// 从配置读取 assemble_buffer_size_bytes
            if (app_config.isMember("assemble_buffer_size_bytes")) {
                m_assemble_buffer_size_bytes = static_cast<uint32_t>(app_config["assemble_buffer_size_bytes"].asUInt());
                Logger::Info() << "Loaded assemble_buffer_size_bytes from config: " << m_assemble_buffer_size_bytes;
            }

            /// 从配置读取 file_io_threads
            m_file_io_threads = static_cast<uint32_t>(app_config.get("file_io_threads", 0).asUInt());
            if (m_file_io_threads > 0) {
                Logger::Info() << "Loaded file_io_threads from config: " << m_file_io_threads;
            }

            const auto backend_value = app_config.get("storage_backend", "local").asString();
            m_storage_backend = ParseStorageBackend(backend_value);
            Logger::Info() << "Loaded storage_backend from config: " << backend_value;

            const auto staging_backend_value =
                app_config.get("upload_staging_backend", "local").asString();
            m_upload_staging_backend = ParseStorageBackend(staging_backend_value);
            Logger::Info() << "Loaded upload_staging_backend from config: " << staging_backend_value;
            if (m_upload_staging_backend == StorageBackend::S3 &&
                m_storage_backend != StorageBackend::S3) {
                throw std::runtime_error(
                    "upload_staging_backend=s3 requires storage_backend=s3"
                );
            }

            if (app_config.isMember("s3")) {
                const auto& s3_config = app_config["s3"];
                m_s3_storage_config.bucket = s3_config.get("bucket", m_s3_storage_config.bucket).asString();
                m_s3_storage_config.region = s3_config.get("region", m_s3_storage_config.region).asString();
                m_s3_storage_config.endpoint = s3_config.get("endpoint", m_s3_storage_config.endpoint).asString();
                m_s3_storage_config.use_ssl = s3_config.get("use_ssl", m_s3_storage_config.use_ssl).asBool();
                m_s3_storage_config.force_path_style =
                    s3_config.get("force_path_style", m_s3_storage_config.force_path_style).asBool();
                m_s3_storage_config.verify_ssl = s3_config.get("verify_ssl", m_s3_storage_config.verify_ssl).asBool();
                m_s3_storage_config.object_prefix = NormalizeObjectPrefix(
                    s3_config.get("object_prefix", m_s3_storage_config.object_prefix).asString(),
                    "objects",
                    "s3.object_prefix"
                );
                m_s3_storage_config.staging_prefix = NormalizeObjectPrefix(
                    s3_config.get("staging_prefix", m_s3_storage_config.staging_prefix).asString(),
                    "staging",
                    "s3.staging_prefix"
                );
                m_s3_storage_config.connect_timeout_ms =
                    s3_config.get("connect_timeout_ms", m_s3_storage_config.connect_timeout_ms).asInt();
                m_s3_storage_config.request_timeout_ms =
                    s3_config.get("request_timeout_ms", m_s3_storage_config.request_timeout_ms).asInt();
            } else {
                m_s3_storage_config.object_prefix = NormalizeObjectPrefix(
                    m_s3_storage_config.object_prefix,
                    "objects",
                    "s3.object_prefix"
                );
                m_s3_storage_config.staging_prefix = NormalizeObjectPrefix(
                    m_s3_storage_config.staging_prefix,
                    "staging",
                    "s3.staging_prefix"
                );
            }

            if (PrefixesOverlap(
                    m_s3_storage_config.object_prefix,
                    m_s3_storage_config.staging_prefix
                )) {
                throw std::runtime_error("S3 object and staging prefixes must not overlap");
            }

            if (m_storage_backend == StorageBackend::S3) {
                if (m_s3_storage_config.bucket.empty()) {
                    throw std::runtime_error("S3 storage backend requires non-empty bucket");
                }
                if (m_s3_storage_config.region.empty()) {
                    throw std::runtime_error("S3 storage backend requires non-empty region");
                }
            }

            /// 从配置读取 upload_rate_limit_per_minute
            if (app_config.isMember("upload_rate_limit_per_minute")) {
                m_upload_rate_limit_per_minute = app_config["upload_rate_limit_per_minute"].asInt();
                Logger::Info() << "Loaded upload_rate_limit_per_minute from config: "
                               << m_upload_rate_limit_per_minute;
            }

            const auto load_int = [&app_config](const char* key, int current_value) -> int {
                const auto configured_value = app_config.get(key, current_value).asInt();
                return configured_value > 0 ? configured_value : current_value;
            };

            const auto load_positive_int = [&app_config](const char* key, int default_value) -> int {
                if (!app_config.isMember(key)) {
                    return default_value;
                }

                const auto& value = app_config[key];
                if (!value.isInt() && !value.isUInt()) {
                    return default_value;
                }

                const auto configured_value = value.asInt64();
                if (configured_value <= 0 ||
                    configured_value > std::numeric_limits<int>::max()) {
                    return default_value;
                }
                return static_cast<int>(configured_value);
            };

            m_download_rate_limit_per_minute =
                load_int("download_rate_limit_per_minute", m_download_rate_limit_per_minute);
            m_folder_rate_limit_per_minute =
                load_int("folder_rate_limit_per_minute", m_folder_rate_limit_per_minute);
            m_admin_rate_limit_per_minute =
                load_int("admin_rate_limit_per_minute", m_admin_rate_limit_per_minute);
            m_share_access_rate_limit_per_minute = load_positive_int(
                "share_access_rate_limit_per_minute",
                DEFAULT_SHARE_ACCESS_RATE_LIMIT_PER_MINUTE
            );
            m_share_browse_rate_limit_per_minute = load_positive_int(
                "share_browse_rate_limit_per_minute",
                DEFAULT_SHARE_BROWSE_RATE_LIMIT_PER_MINUTE
            );
            m_share_download_rate_limit_per_minute = load_positive_int(
                "share_download_rate_limit_per_minute",
                DEFAULT_SHARE_DOWNLOAD_RATE_LIMIT_PER_MINUTE
            );
            m_register_rate_limit_per_window =
                load_int("register_rate_limit_per_window", m_register_rate_limit_per_window);
            m_upload_rate_limit_window_seconds =
                load_int("upload_rate_limit_window_seconds", m_upload_rate_limit_window_seconds);
            m_download_rate_limit_window_seconds =
                load_int("download_rate_limit_window_seconds", m_download_rate_limit_window_seconds);
            m_folder_rate_limit_window_seconds =
                load_int("folder_rate_limit_window_seconds", m_folder_rate_limit_window_seconds);
            m_admin_rate_limit_window_seconds =
                load_int("admin_rate_limit_window_seconds", m_admin_rate_limit_window_seconds);
            m_share_access_rate_limit_window_seconds = load_positive_int(
                "share_access_rate_limit_window_seconds",
                DEFAULT_SHARE_ACCESS_RATE_LIMIT_WINDOW_SECONDS
            );
            m_share_browse_rate_limit_window_seconds = load_positive_int(
                "share_browse_rate_limit_window_seconds",
                DEFAULT_SHARE_BROWSE_RATE_LIMIT_WINDOW_SECONDS
            );
            m_share_download_rate_limit_window_seconds = load_positive_int(
                "share_download_rate_limit_window_seconds",
                DEFAULT_SHARE_DOWNLOAD_RATE_LIMIT_WINDOW_SECONDS
            );
            m_register_rate_limit_window_seconds =
                load_int("register_rate_limit_window_seconds", m_register_rate_limit_window_seconds);
        } else {
            Logger::Warn() << "'disk' section not found in custom config, using default values";
        }

        if (const auto* instance_id = std::getenv("DISK_INSTANCE_ID"); instance_id != nullptr) {
            ValidateInstanceId(instance_id, "DISK_INSTANCE_ID");
            m_instance_id = instance_id;
        }
        if (const auto* process_role = std::getenv("DISK_PROCESS_ROLE"); process_role != nullptr) {
            m_process_role = ParseProcessRole(process_role);
            m_process_role_explicit = true;
        }
        Logger::Info() << "Using process_role: " << ProcessRoleName(m_process_role)
                       << ", instance_id: " << m_instance_id
                       << ", upload_finalize_lease_seconds: " << m_upload_finalize_lease_seconds;

        /// 读取数据库和 Redis 连接池大小
        {
            std::ifstream ifs("config.json");
            if (ifs.is_open()) {
                Json::Value root;
                Json::CharReaderBuilder builder;
                std::string errors;
                if (Json::parseFromStream(builder, ifs, &root, &errors)) {
                    if (root.isMember("db_clients") && root["db_clients"].isArray() &&
                        !root["db_clients"].empty()) {
                        m_db_pool_size =
                            root["db_clients"][0].get("num_connection_number", 0).asInt64();
                    }
                    if (root.isMember("redis_clients") && root["redis_clients"].isArray() &&
                        !root["redis_clients"].empty()) {
                        m_redis_pool_size =
                            root["redis_clients"][0].get("number_of_connections", 0).asInt64();
                    }
                }
            }
        }
    }

    auto ConfigMgr::GetJwtSecret() const -> std::string {
        constexpr size_t MIN_SECRET_LENGTH = 32;
        const auto* env_secret = std::getenv("JWT_SECRET");

        if (env_secret != nullptr && std::strlen(env_secret) >= MIN_SECRET_LENGTH) {
            return env_secret;
        }

        std::string error_msg;
        if (env_secret == nullptr) {
            error_msg = "JWT_SECRET environment variable is not set. " "A minimum of 32 characters is required in all environments.";
        } else {
            error_msg = "JWT_SECRET is too short (" + std::to_string(std::strlen(env_secret)) + " chars). A minimum of " + std::to_string(MIN_SECRET_LENGTH) + " characters is required in all environments.";
        }
        Logger::Error() << error_msg;
        throw std::runtime_error(error_msg);
    }

    auto ConfigMgr::GetAccessTokenExpireSeconds() const -> int {
        return m_access_token_expire_seconds;
    }

    auto ConfigMgr::GetRefreshTokenExpireSeconds() const -> int {
        return m_refresh_token_expire_seconds;
    }

    auto ConfigMgr::GetInstanceId() const noexcept -> std::string {
        return m_instance_id;
    }

    auto ConfigMgr::GetProcessRole() const noexcept -> ProcessRole {
        return m_process_role;
    }

    auto ConfigMgr::GetWorkerPollIntervalMs() const noexcept -> uint32_t {
        return m_worker_poll_interval_ms;
    }

    auto ConfigMgr::GetWorkerClaimBatchSize() const noexcept -> uint32_t {
        return m_worker_claim_batch_size;
    }

    auto ConfigMgr::GetWorkerConcurrency() const noexcept -> uint32_t {
        return m_worker_concurrency;
    }

    auto ConfigMgr::GetWorkerLeaseDurationSeconds() const noexcept -> uint32_t {
        return m_worker_lease_duration_seconds;
    }

    auto ConfigMgr::GetWorkerDrainTimeoutSeconds() const noexcept -> uint32_t {
        return m_worker_drain_timeout_seconds;
    }

    auto ConfigMgr::GetUploadFinalizeLeaseSeconds() const noexcept -> uint32_t {
        return m_upload_finalize_lease_seconds;
    }

    /// ==================== 存储配置 ====================

    auto ConfigMgr::GetStorageBasePath() const noexcept -> std::string {
        return m_storage_base_path;
    }

    auto ConfigMgr::GetTempUploadPath() const noexcept -> std::string {
        return m_temp_upload_path;
    }

    auto ConfigMgr::GetChunkSize() const noexcept -> uint32_t {
        return m_chunk_size;
    }

    auto ConfigMgr::GetMaxFileSize() const noexcept -> uint64_t {
        return m_max_file_size;
    }

    auto ConfigMgr::GetUploadTaskExpirySeconds() const noexcept -> int {
        return m_upload_task_expiry_seconds;
    }

    auto ConfigMgr::GetAssemblyMaxConcurrent() const noexcept -> uint32_t {
        return m_assembly_max_concurrent;
    }

    auto ConfigMgr::GetAssembleBufferSizeBytes() const noexcept -> uint32_t {
        return m_assemble_buffer_size_bytes;
    }

    auto ConfigMgr::GetFileIoThreads() const noexcept -> uint32_t {
        return m_file_io_threads;
    }

    auto ConfigMgr::GetStorageBackend() const noexcept -> StorageBackend {
        return m_storage_backend;
    }

    auto ConfigMgr::GetUploadStagingBackend() const noexcept -> StorageBackend {
        return m_upload_staging_backend;
    }

    auto ConfigMgr::GetS3StorageConfig() const noexcept -> S3StorageConfig {
        return m_s3_storage_config;
    }

    auto ConfigMgr::GetUploadRateLimitPerMinute() const noexcept -> int {
        return m_upload_rate_limit_per_minute;
    }

    auto ConfigMgr::GetDownloadRateLimitPerMinute() const noexcept -> int {
        return m_download_rate_limit_per_minute;
    }

    auto ConfigMgr::GetFolderRateLimitPerMinute() const noexcept -> int {
        return m_folder_rate_limit_per_minute;
    }

    auto ConfigMgr::GetAdminRateLimitPerMinute() const noexcept -> int {
        return m_admin_rate_limit_per_minute;
    }

    auto ConfigMgr::GetShareAccessRateLimitPerMinute() const noexcept -> int {
        return m_share_access_rate_limit_per_minute;
    }

    auto ConfigMgr::GetShareBrowseRateLimitPerMinute() const noexcept -> int {
        return m_share_browse_rate_limit_per_minute;
    }

    auto ConfigMgr::GetShareDownloadRateLimitPerMinute() const noexcept -> int {
        return m_share_download_rate_limit_per_minute;
    }

    auto ConfigMgr::GetRegisterRateLimitPerWindow() const noexcept -> int {
        return m_register_rate_limit_per_window;
    }

    auto ConfigMgr::GetUploadRateLimitWindowSeconds() const noexcept -> int {
        return m_upload_rate_limit_window_seconds;
    }

    auto ConfigMgr::GetDownloadRateLimitWindowSeconds() const noexcept -> int {
        return m_download_rate_limit_window_seconds;
    }

    auto ConfigMgr::GetFolderRateLimitWindowSeconds() const noexcept -> int {
        return m_folder_rate_limit_window_seconds;
    }

    auto ConfigMgr::GetAdminRateLimitWindowSeconds() const noexcept -> int {
        return m_admin_rate_limit_window_seconds;
    }

    auto ConfigMgr::GetShareAccessRateLimitWindowSeconds() const noexcept -> int {
        return m_share_access_rate_limit_window_seconds;
    }

    auto ConfigMgr::GetShareBrowseRateLimitWindowSeconds() const noexcept -> int {
        return m_share_browse_rate_limit_window_seconds;
    }

    auto ConfigMgr::GetShareDownloadRateLimitWindowSeconds() const noexcept -> int {
        return m_share_download_rate_limit_window_seconds;
    }

    auto ConfigMgr::GetRegisterRateLimitWindowSeconds() const noexcept -> int {
        return m_register_rate_limit_window_seconds;
    }

    /// ==================== 数据库配置 ====================

    auto ConfigMgr::GetDatabasePassword() const -> std::string {
        const auto* env_password = std::getenv("DATABASE_PASSWORD");
        if (env_password != nullptr && std::strlen(env_password) > 0) {
            return env_password;
        }
        return "";
    }

    auto ConfigMgr::GetRedisPassword() const -> std::string {
        const auto* env_password = std::getenv("REDIS_PASSWORD");
        if (env_password != nullptr && std::strlen(env_password) > 0) {
            return env_password;
        }
        return "";
    }

    auto ConfigMgr::IsSecureMode() const -> bool {
        const auto* secure_mode = std::getenv("DISK_SECURE_MODE");
        return secure_mode != nullptr && (std::strcmp(secure_mode, "true") == 0 || std::strcmp(secure_mode, "1") == 0);
    }

    auto ConfigMgr::ValidateSecureConfig() const -> void {
        /// ALWAYS validate JWT_SECRET in all environments
        constexpr size_t MIN_JWT_SECRET_LENGTH = 32;
        const auto* jwt_secret = std::getenv("JWT_SECRET");
        if (jwt_secret == nullptr || std::strlen(jwt_secret) < MIN_JWT_SECRET_LENGTH) {
            std::string error_msg = "JWT_SECRET environment variable is missing or too short. " "A minimum of " + std::to_string(MIN_JWT_SECRET_LENGTH) + " characters is required in all environments.";
            Logger::Error() << error_msg;
            throw std::runtime_error(error_msg);
        }

        /// Only validate DATABASE_PASSWORD and REDIS_PASSWORD in secure mode
        if (!IsSecureMode()) {
            Logger::Info() << "Running in development mode - skipping DATABASE/REDIS password validation";
            return;
        }

        Logger::Info() << "Running in secure mode - validating required environment variables";

        std::vector<std::string> missing_vars;

        const auto* db_password = std::getenv("DATABASE_PASSWORD");
        if (db_password == nullptr || std::strlen(db_password) == 0) {
            missing_vars.emplace_back("DATABASE_PASSWORD");
        }

        const auto* redis_password = std::getenv("REDIS_PASSWORD");
        if (redis_password == nullptr || std::strlen(redis_password) == 0) {
            missing_vars.emplace_back("REDIS_PASSWORD");
        }

        if (!missing_vars.empty()) {
            std::string error_msg = "Missing required environment variables in secure mode: ";
            for (size_t i = 0; i < missing_vars.size(); ++i) {
                if (i > 0) {
                    error_msg += ", ";
                }
                error_msg += missing_vars[i];
            }
            Logger::Error() << error_msg;
            throw std::runtime_error(error_msg);
        }

        if (!m_process_role_explicit || m_process_role == ProcessRole::All) {
            const std::string error_msg =
                "Secure mode requires an explicit api or worker process role";
            Logger::Error() << error_msg;
            throw std::runtime_error(error_msg);
        }

        Logger::Info() << "All required environment variables validated successfully";
    }

    auto ConfigMgr::GetDbPoolSize() const noexcept -> int64_t {
        return m_db_pool_size;
    }

    auto ConfigMgr::GetRedisPoolSize() const noexcept -> int64_t {
        return m_redis_pool_size;
    }

} // namespace disk::utils
