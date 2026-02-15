/**
 * @file ConfigMgr.cpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 配置管理类实现
 * @version 0.1
 * @date 2026-01-18
 *
 * @copyright Copyright (c) 2026
 *
 */
#include "ConfigMgr.hpp"

#include <mutex>

#include <drogon/utils/Utilities.h>

namespace disk::utils {
    ConfigMgr::ConfigMgr() = default;

    auto ConfigMgr::GetJwtSecret() const -> std::string {
        constexpr size_t MIN_SECRET_LENGTH = 32;
        const auto* env_secret = std::getenv("JWT_SECRET");

        if (env_secret != nullptr && std::strlen(env_secret) >= MIN_SECRET_LENGTH) {
            LOG_INFO << "从环境变量读取 JWT 密钥";
            return env_secret;
        }
        if (env_secret != nullptr) {
            LOG_ERROR << "JWT_SECRET 长度不足，至少需要 " << MIN_SECRET_LENGTH << " 字符";
        }

        static std::once_flag warning_flag;
        std::call_once(warning_flag, [] {
            LOG_WARN << "JWT_SECRET 未正确配置，使用默认密钥（仅开发环境）";
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
