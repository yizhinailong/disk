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
     * - JWT密钥: 环境变量 JWT_SECRET (最小32字符，所有环境必须设置)
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
         * Always validates:
         * - JWT_SECRET (min 32 chars, required in all environments)
         *
         * In secure mode (when DISK_SECURE_MODE=true), additionally validates:
         * - DATABASE_PASSWORD
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

        /**
         * @brief 获取分片组装最大并发数
         * @return uint32_t 最大并发数（默认 4）
         */
        [[nodiscard]]
        auto GetAssemblyMaxConcurrent() const noexcept -> uint32_t;

        /**
         * @brief 获取组装缓冲区大小（字节）
         * @return uint32_t 缓冲区大小（默认 256KB）
         */
        [[nodiscard]]
        auto GetAssembleBufferSizeBytes() const noexcept -> uint32_t;

        /**
         * @brief 获取文件IO线程数
         * @return uint32_t 线程数（0 表示使用默认值）
         */
        [[nodiscard]]
        auto GetFileIoThreads() const noexcept -> uint32_t;

        /**
         * @brief 获取上传接口每分钟限流阈值
         * @return int 每分钟请求数上限（默认 60）
         */
        [[nodiscard]]
        auto GetUploadRateLimitPerMinute() const noexcept -> int;

        // ==================== 数据库配置 ====================

        /**
         * @brief 获取数据库密码（从环境变量DATABASE_PASSWORD读取）
         * @return std::string 数据库密码，开发环境可返回空字符串
         */
        [[nodiscard]]
        auto GetDatabasePassword() const -> std::string;

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

        /**
         * @brief 获取数据库连接池大小
         * @return int64_t 连接池大小
         */
        [[nodiscard]]
        auto GetDbPoolSize() const noexcept -> int64_t;

        /**
         * @brief 获取 Redis 连接池大小
         * @return int64_t 连接池大小
         */
        [[nodiscard]]
        auto GetRedisPoolSize() const noexcept -> int64_t;

    private:
        // JWT 配置
        int m_access_token_expire_seconds{ 7200 };
        int m_refresh_token_expire_seconds{ 604800 };

        // 存储配置
        std::string m_storage_base_path{ "build/uploaded" };
        std::string m_temp_upload_path{ "build/temp_uploads" };
        uint32_t m_chunk_size{ 5242880 };
        uint64_t m_max_file_size{ 10737418240 };
        int m_upload_task_expiry_seconds{ 86400 };
        uint32_t m_assembly_max_concurrent{ 4 };
        uint32_t m_assemble_buffer_size_bytes{ 262144 };
        int m_upload_rate_limit_per_minute{ 60 };
        uint32_t m_file_io_threads{ 0 };

        int64_t m_db_pool_size{ 0 };
        int64_t m_redis_pool_size{ 0 };
    };

} // namespace disk::utils
