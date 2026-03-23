/**
 * @file ConfigMgr.hpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 配置管理类（单例）
 *
 * @copyright Copyright (c) 2026
 *
 */
#pragma once

#include "Singleton.hpp"

namespace disk::utils {

    /**
     * @brief 配置管理类（单例）
     *
     * 职责：
     * - 管理应用配置参数（JWT密钥、令牌过期时间等）
     * - 提供统一的配置访问接口
     *
     * 配置来源：
     * - JWT密钥: 环境变量 JWT_SECRET (最小32字符) 或默认值
     * - 令牌过期时间: 默认值
     * - 存储路径: config.json 中的 custom_config.disk.* 字段或默认值
     *
     * 使用方式：
     * @code
     * auto jwt_secret = ConfigMgr::GetInstance()->GetJwtSecret();
     * auto expire_time = ConfigMgr::GetInstance()->GetAccessTokenExpireSeconds();
     * @endcode
     *
     * 线程安全：
     * - 继承自 Singleton 基类，保证单例和线程安全
     * - Getter 方法为 const，保证只读访问
     */
    class ConfigMgr : public Singleton<ConfigMgr> {
        friend class Singleton<ConfigMgr>;

    public:
        ConfigMgr();

        /**
         * @brief Load project configuration from Drogon custom config
         * This must be called after drogon::app().loadConfigFile()
         */
        auto LoadConfig() -> void;

        /**
         * @brief Validate required environment variables for secure mode
         * @throws std::runtime_error if required secrets are missing in production
         *
         * In production mode (when DISK_SECURE_MODE=true), this validates:
         * - JWT_SECRET (min 32 chars)
         * - MYSQL_PASSWORD
         * - REDIS_PASSWORD
         */
        auto ValidateSecureConfig() const -> void;

        ~ConfigMgr() = default;
        ConfigMgr(const ConfigMgr&) = delete;
        ConfigMgr& operator=(const ConfigMgr&) = delete;
        ConfigMgr(ConfigMgr&&) = delete;
        ConfigMgr& operator=(ConfigMgr&&) = delete;

        /**
         * @brief 获取JWT签名密钥
         * @return std::string JWT签名密钥
         */
        [[nodiscard]]
        auto GetJwtSecret() const -> std::string;

        /**
         * @brief 获取访问令牌过期时间（秒）
         * @return int 过期时间（秒）
         */
        [[nodiscard]]
        auto GetAccessTokenExpireSeconds() const -> int;

        /**
         * @brief 获取刷新令牌过期时间（秒）
         * @return int 过期时间（秒）
         */
        [[nodiscard]]
        auto GetRefreshTokenExpireSeconds() const -> int;

        // ==================== 存储配置 ====================

        /**
         * @brief 获取存储基础路径
         * @return std::string 存储基础路径
         */
        [[nodiscard]]
        auto GetStorageBasePath() const noexcept -> std::string;

        /**
         * @brief 获取临时上传路径
         * @return std::string 临时上传路径
         */
        [[nodiscard]]
        auto GetTempUploadPath() const noexcept -> std::string;

        /**
         * @brief 获取分片大小（字节）
         * @return uint32_t 分片大小（默认 5MB）
         */
        [[nodiscard]]
        auto GetChunkSize() const noexcept -> uint32_t;

        /**
         * @brief 获取最大文件大小（字节）
         * @return uint64_t 最大文件大小（默认 10GB）
         */
        [[nodiscard]]
        auto GetMaxFileSize() const noexcept -> uint64_t;

        /**
         * @brief 获取上传任务过期时间（秒）
         * @return int 过期时间（默认 24小时）
         */
        [[nodiscard]]
        auto GetUploadTaskExpirySeconds() const noexcept -> int;

        // ==================== 数据库配置 ====================

        /**
         * @brief 获取MySQL密码（从环境变量MYSQL_PASSWORD读取）
         * @return std::string MySQL密码，开发环境可返回空字符串
         */
        [[nodiscard]]
        auto GetMySqlPassword() const -> std::string;

        /**
         * @brief 获取Redis密码（从环境变量REDIS_PASSWORD读取）
         * @return std::string Redis密码，开发环境可返回空字符串
         */
        [[nodiscard]]
        auto GetRedisPassword() const -> std::string;

        /**
         * @brief 检查是否为安全模式
         * @return bool 当DISK_SECURE_MODE=true时返回true
         */
        [[nodiscard]]
        auto IsSecureMode() const -> bool;

    private:
        // JWT 配置
        std::string m_jwt_secret{ "dev-secret-key-change-in-production-min-32-chars" };
        int m_access_token_expire_seconds{ 7200 };
        int m_refresh_token_expire_seconds{ 604800 };

        // 存储配置
        std::string m_storage_base_path{ "build/uploaded" };
        std::string m_temp_upload_path{ "build/temp_uploads" };
        uint32_t m_chunk_size{ 5242880 };
        uint64_t m_max_file_size{ 10737418240 };
        int m_upload_task_expiry_seconds{ 86400 };
    };

} // namespace disk::utils
