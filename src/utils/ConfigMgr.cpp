/**
 * @file ConfigMgr.cpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 配置管理类实现
 *
 * @copyright Copyright (c) 2026
 *
 */
#include "ConfigMgr.hpp"

#include <mutex>

#include <drogon/utils/Utilities.h>

namespace disk::utils {
    ConfigMgr::ConfigMgr() = default;

    auto ConfigMgr::LoadConfig() -> void {
        const auto& custom_config = drogon::app().getCustomConfig();

        if (custom_config.isMember("disk")) {
            const auto& app_config = custom_config["disk"];

            // Read storage_base_path from config
            if (app_config.isMember("storage_base_path")) {
                m_storage_base_path = app_config["storage_base_path"].asString();
                LOG_INFO << "Loaded storage_base_path from config: " << m_storage_base_path;
            } else {
                LOG_WARN << "storage_base_path not found in config, using default: " << m_storage_base_path;
            }

            // Read temp_upload_path from config
            if (app_config.isMember("temp_upload_path")) {
                m_temp_upload_path = app_config["temp_upload_path"].asString();
                LOG_INFO << "Loaded temp_upload_path from config: " << m_temp_upload_path;
            } else {
                LOG_WARN << "temp_upload_path not found in config, using default: " << m_temp_upload_path;
            }

            // Read chunk_size from config
            if (app_config.isMember("chunk_size")) {
                m_chunk_size = static_cast<uint32_t>(app_config["chunk_size"].asUInt());
                LOG_INFO << "Loaded chunk_size from config: " << m_chunk_size;
            }

            // Read max_file_size from config
            if (app_config.isMember("max_file_size")) {
                m_max_file_size = static_cast<uint64_t>(app_config["max_file_size"].asUInt64());
                LOG_INFO << "Loaded max_file_size from config: " << m_max_file_size;
            }

            // Read upload_task_expiry_seconds from config
            if (app_config.isMember("upload_task_expiry_seconds")) {
                m_upload_task_expiry_seconds = app_config["upload_task_expiry_seconds"].asInt();
                LOG_INFO << "Loaded upload_task_expiry_seconds from config: " << m_upload_task_expiry_seconds;
            }
        } else {
            LOG_WARN << "'disk' section not found in custom config, using default values";
        }
    }

    auto ConfigMgr::GetJwtSecret() const -> std::string {
        constexpr size_t MIN_SECRET_LENGTH = 32;
        const auto* env_secret = std::getenv("JWT_SECRET");

        if (env_secret != nullptr && std::strlen(env_secret) >= MIN_SECRET_LENGTH) {
            LOG_INFO << "Reading JWT secret from environment variable";
            return env_secret;
        }
        if (env_secret != nullptr) {
            LOG_ERROR << "JWT_SECRET length is insufficient, at least " << MIN_SECRET_LENGTH
                      << " characters required";
        }

        static std::once_flag warning_flag;
        std::call_once(warning_flag, [] {
            LOG_WARN
                << "JWT_SECRET not properly configured, using default secret (development only)";
        });

        return m_jwt_secret;
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

} // namespace disk::utils
