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
    ConfigMgr::ConfigMgr() = default;

    auto ConfigMgr::LoadConfig() -> void {
        const auto& custom_config = drogon::app().getCustomConfig();

        if (custom_config.isMember("disk")) {
            const auto& app_config = custom_config["disk"];

            // 从配置读取 storage_base_path
            if (app_config.isMember("storage_base_path")) {
                m_storage_base_path = app_config["storage_base_path"].asString();
                LOG_INFO << "Loaded storage_base_path from config: " << m_storage_base_path;
            } else {
                LOG_WARN << "storage_base_path not found in config, using default: " << m_storage_base_path;
            }

            // 从配置读取 temp_upload_path
            if (app_config.isMember("temp_upload_path")) {
                m_temp_upload_path = app_config["temp_upload_path"].asString();
                LOG_INFO << "Loaded temp_upload_path from config: " << m_temp_upload_path;
            } else {
                LOG_WARN << "temp_upload_path not found in config, using default: " << m_temp_upload_path;
            }

            // 从配置读取 chunk_size
            if (app_config.isMember("chunk_size")) {
                m_chunk_size = static_cast<uint32_t>(app_config["chunk_size"].asUInt());
                LOG_INFO << "Loaded chunk_size from config: " << m_chunk_size;
            }

            // 从配置读取 max_file_size
            if (app_config.isMember("max_file_size")) {
                m_max_file_size = static_cast<uint64_t>(app_config["max_file_size"].asUInt64());
                LOG_INFO << "Loaded max_file_size from config: " << m_max_file_size;
            }

            // 从配置读取 upload_task_expiry_seconds
            if (app_config.isMember("upload_task_expiry_seconds")) {
                m_upload_task_expiry_seconds = app_config["upload_task_expiry_seconds"].asInt();
                LOG_INFO << "Loaded upload_task_expiry_seconds from config: " << m_upload_task_expiry_seconds;
            }

            // 从配置读取 assembly_max_concurrent
            if (app_config.isMember("assembly_max_concurrent")) {
                m_assembly_max_concurrent = static_cast<uint32_t>(app_config["assembly_max_concurrent"].asUInt());
                LOG_INFO << "Loaded assembly_max_concurrent from config: " << m_assembly_max_concurrent;
            }

            // 从配置读取 assemble_buffer_size_bytes
            if (app_config.isMember("assemble_buffer_size_bytes")) {
                m_assemble_buffer_size_bytes = static_cast<uint32_t>(app_config["assemble_buffer_size_bytes"].asUInt());
                LOG_INFO << "Loaded assemble_buffer_size_bytes from config: " << m_assemble_buffer_size_bytes;
            }
        } else {
            LOG_WARN << "'disk' section not found in custom config, using default values";
        }

        // 读取数据库和 Redis 连接池大小
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
        LOG_ERROR << error_msg;
        throw std::runtime_error(error_msg);
    }

    auto ConfigMgr::GetAccessTokenExpireSeconds() const -> int {
        return m_access_token_expire_seconds;
    }

    auto ConfigMgr::GetRefreshTokenExpireSeconds() const -> int {
        return m_refresh_token_expire_seconds;
    }

    // ==================== 存储配置 ====================

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

    // ==================== 数据库配置 ====================

    auto ConfigMgr::GetMySqlPassword() const -> std::string {
        const auto* env_password = std::getenv("MYSQL_PASSWORD");
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
        // ALWAYS validate JWT_SECRET in all environments
        constexpr size_t MIN_JWT_SECRET_LENGTH = 32;
        const auto* jwt_secret = std::getenv("JWT_SECRET");
        if (jwt_secret == nullptr || std::strlen(jwt_secret) < MIN_JWT_SECRET_LENGTH) {
            std::string error_msg = "JWT_SECRET environment variable is missing or too short. " "A minimum of " + std::to_string(MIN_JWT_SECRET_LENGTH) + " characters is required in all environments.";
            LOG_ERROR << error_msg;
            throw std::runtime_error(error_msg);
        }

        // Only validate MYSQL_PASSWORD and REDIS_PASSWORD in secure mode
        if (!IsSecureMode()) {
            LOG_INFO << "Running in development mode - skipping MYSQL/REDIS password validation";
            return;
        }

        LOG_INFO << "Running in secure mode - validating required environment variables";

        std::vector<std::string> missing_vars;

        const auto* mysql_password = std::getenv("MYSQL_PASSWORD");
        if (mysql_password == nullptr || std::strlen(mysql_password) == 0) {
            missing_vars.emplace_back("MYSQL_PASSWORD");
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
            LOG_ERROR << error_msg;
            throw std::runtime_error(error_msg);
        }

        LOG_INFO << "All required environment variables validated successfully";
    }

    auto ConfigMgr::GetDbPoolSize() const noexcept -> int64_t {
        return m_db_pool_size;
    }

    auto ConfigMgr::GetRedisPoolSize() const noexcept -> int64_t {
        return m_redis_pool_size;
    }

} // namespace disk::utils
