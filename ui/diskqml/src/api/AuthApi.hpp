/**
 * @file AuthApi.hpp
 * @brief 认证服务 API 客户端
 * @details 提供用户注册、登录、登出、令牌刷新等认证相关的 HTTP API 调用
 * @author LiuFeng (liufeng.code@outlook.com)
 *
 * @copyright Copyright (c) 2026
 */
#pragma once

#include <QObject>
#include <QString>

#include <dtos/ApiEnvelope.hpp>
#include <dtos/AuthDtos.hpp>

namespace disk::qml::api {

    using AuthApiCallback = models::ApiCallback;

    class ApiClient;

    class AuthApi {
    public:
        /**
         * @brief 构造认证服务 API 客户端
         *
         * @param client API 客户端指针，调用者需确保该指针的生命周期长于此实例
         */
        explicit AuthApi(ApiClient* client);

        /**
         * @brief POST /api/auth/register — 创建新用户账号
         *
         * @param username 用户名
         * @param email 邮箱地址
         * @param password 密码（明文，服务端进行哈希处理）
         * @param ctx 上下文 QObject，销毁时取消回调
         * @param cb 服务器响应回调
         */
        virtual auto Register(
            const QString& username,
            const QString& email,
            const QString& password,
            QObject* ctx,
            AuthApiCallback cb
        ) -> void;

        /**
         * @brief POST /api/auth/login — 用户登录并获取令牌
         *
         * @param account 账号（用户名或邮箱）
         * @param password 密码
         * @param ctx 上下文 QObject，销毁时取消回调
         * @param cb 服务器响应回调
         */
        virtual auto Login(const QString& account, const QString& password, QObject* ctx, AuthApiCallback cb) -> void;

        /**
         * @brief POST /api/auth/refresh — 使用刷新令牌获取新令牌
         *
         * @param refreshToken 刷新令牌（登录时获取，仅可使用一次）
         * @param ctx 上下文 QObject，销毁时取消回调
         * @param cb 服务器响应回调
         */
        virtual auto Refresh(const QString& refreshToken, QObject* ctx, AuthApiCallback cb) -> void;

        /**
         * @brief POST /api/auth/logout — 撤销访问令牌
         *
         * 使用 @p accessToken 作为 Bearer 令牌发起此请求，不修改共享工厂的令牌状态。
         *
         * @param accessToken 当前访问令牌
         * @param ctx 上下文 QObject，销毁时取消回调
         * @param cb 服务器响应回调
         */
        virtual auto Logout(const QString& accessToken, QObject* ctx, AuthApiCallback cb) -> void;

        virtual ~AuthApi() = default;

    private:
        ApiClient* m_client;
    };

} // namespace disk::qml::api
