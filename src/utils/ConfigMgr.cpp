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
#include <stdexcept>
#include <vector>

#include <drogon/utils/Utilities.h>

namespace disk::utils {
    namespace {
        auto ParseStorageBackend(std::string value) -> StorageBackend {
            if (value == "local") {
                return StorageBackend::Local;
            }
            if (value == "s3") {
                return StorageBackend::S3;
            }
            throw std::runtime_error("Invalid storage_backend: " + value);
        }

        auto NormalizeObjectPrefix(std::string prefix) -> std::string {
            while (!prefix.empty() && prefix.front() == '/') {
                prefix.erase(prefix.begin());
            }
            while (!prefix.empty() && prefix.back() == '/') {
                prefix.pop_back();
            }
            return prefix.empty() ? "objects" : prefix;
        }
    }

    ConfigMgr::ConfigMgr() = default;

    auto ConfigMgr::LoadConfig() -> void {
        const auto& custom_config = drogon::app().getCustomConfig();
        m_storage_backend = StorageBackend::Local;
        m_s3_storage_config = S3StorageConfig{};

        if (custom_config.isMember("disk")) {
            const auto& app_config = custom_config["disk"];

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
                    s3_config.get("object_prefix", m_s3_storage_config.object_prefix).asString()
                );
                m_s3_storage_config.connect_timeout_ms =
                    s3_config.get("connect_timeout_ms", m_s3_storage_config.connect_timeout_ms).asInt();
                m_s3_storage_config.request_timeout_ms =
                    s3_config.get("request_timeout_ms", m_s3_storage_config.request_timeout_ms).asInt();
            } else {
                m_s3_storage_config.object_prefix = NormalizeObjectPrefix(m_s3_storage_config.object_prefix);
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

            m_download_rate_limit_per_minute =
                load_int("download_rate_limit_per_minute", m_download_rate_limit_per_minute);
            m_folder_rate_limit_per_minute =
                load_int("folder_rate_limit_per_minute", m_folder_rate_limit_per_minute);
            m_admin_rate_limit_per_minute =
                load_int("admin_rate_limit_per_minute", m_admin_rate_limit_per_minute);
            m_share_public_rate_limit_per_minute =
                load_int("share_public_rate_limit_per_minute", m_share_public_rate_limit_per_minute);
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
            m_share_public_rate_limit_window_seconds = load_int(
                "share_public_rate_limit_window_seconds",
                m_share_public_rate_limit_window_seconds
            );
            m_register_rate_limit_window_seconds =
                load_int("register_rate_limit_window_seconds", m_register_rate_limit_window_seconds);
        } else {
            Logger::Warn() << "'disk' section not found in custom config, using default values";
        }

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

    auto ConfigMgr::GetSharePublicRateLimitPerMinute() const noexcept -> int {
        return m_share_public_rate_limit_per_minute;
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

    auto ConfigMgr::GetSharePublicRateLimitWindowSeconds() const noexcept -> int {
        return m_share_public_rate_limit_window_seconds;
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

        Logger::Info() << "All required environment variables validated successfully";
    }

    auto ConfigMgr::GetDbPoolSize() const noexcept -> int64_t {
        return m_db_pool_size;
    }

    auto ConfigMgr::GetRedisPoolSize() const noexcept -> int64_t {
        return m_redis_pool_size;
    }

} ///< namespace disk::utils
