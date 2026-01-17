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

    class ConfigMgr : public Singleton<ConfigMgr> {
        friend class Singleton<ConfigMgr>;

    public:
        ConfigMgr();
        ~ConfigMgr() = default;
        ConfigMgr(const ConfigMgr&) = delete;
        ConfigMgr& operator=(const ConfigMgr&) = delete;
        ConfigMgr(ConfigMgr&&) = delete;
        ConfigMgr& operator=(ConfigMgr&&) = delete;

        [[nodiscard]]
        auto GetJwtSecret() const -> std::string;

        [[nodiscard]]
        auto GetAccessTokenExpireSeconds() const -> int;

        [[nodiscard]]
        auto GetRefreshTokenExpireSeconds() const -> int;

    private:
        const Json::Value& m_custom_config;
        std::string m_jwt_secret{ "dev-secret-key-change-in-production-min-32-chars" };
        int m_access_token_expire_seconds{ 7200 };
        int m_refresh_token_expire_seconds{ 604800 };
    };

} // namespace disk::utils
