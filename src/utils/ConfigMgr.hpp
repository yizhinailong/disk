/**
 * @file ConfigMgr.hpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 配置管理类（单例）
 * @version 0.1
 * @date 2026-01-18
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
     * - JWT密钥: 环境变量 JWT_SECRET 或默认值
     * - 令牌过期时间: 硬编码默认值
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

    private:
        std::string m_jwt_secret{ "dev-secret-key-change-in-production-min-32-chars" };
        int m_access_token_expire_seconds{ 7200 };
        int m_refresh_token_expire_seconds{ 604800 };
    };

} // namespace disk::utils
