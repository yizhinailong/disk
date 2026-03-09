/**
 * @file UserApi.hpp
 * @brief 用户 API 客户端
 * @details 提供用户资料、存储空间、密码管理等用户相关的 HTTP API 调用
 * @author LiuFeng (liufeng.code@outlook.com)
 * @version 0.1
 * @date 2026-03-05
 *
 * @copyright Copyright (c) 2026
 */
#pragma once

#include <QObject>
#include <QString>

#include <dtos/ApiEnvelope.hpp>

namespace disk::qml::api {

    using UserApiCallback = models::ApiCallback;

    class ApiClient;

    /**
     * @brief 用户相关 API 封装（均需 JWT 认证）
     *
     * @details
     * 所有方法使用 ApiClient 上配置的共享 Bearer 令牌。
     * 调用者需确保在调用这些方法前已设置令牌（通过 ApiClient::SetBearerToken）。
     *
     * 封装的端点：
     * - GET   /api/user/profile  → UserProfileResponse
     * - GET   /api/user/storage  → StorageResponse
     * - PATCH /api/user/profile  → UpdateProfileRequest
     * - PUT   /api/user/password → ChangePasswordRequest
     */
    class UserApi {
    public:
        /**
         * @brief 构造用户 API 客户端
         *
         * @param client API 客户端指针，调用者需确保该指针的生命周期长于此实例
         */
        explicit UserApi(ApiClient* client);

        /**
         * @brief GET /api/user/profile — 获取已认证用户的资料
         *
         * @details
         * 响应数据结构: { "user": { id, username, email, nickname, avatar,
         *                storage_used, storage_quota, file_count, folder_count,
         *                created_at, updated_at } }
         *
         * @param ctx 上下文 QObject，销毁时取消回调
         * @param cb 服务器响应回调
         */
        virtual auto GetProfile(QObject* ctx, UserApiCallback cb) -> void;

        /**
         * @brief GET /api/user/storage — 获取存储使用量统计
         *
         * @details
         * 响应数据结构: { "storage": { used, quota, percentage,
         *                file_count, folder_count, categories: [...] } }
         *
         * @param ctx 上下文 QObject，销毁时取消回调
         * @param cb 服务器响应回调
         */
        virtual auto GetStorage(QObject* ctx, UserApiCallback cb) -> void;

        /**
         * @brief PATCH /api/user/profile — 更新已认证用户的资料
         *
         * @details
         * @p nickname 和 @p avatar 至少有一个非空。
         * 空字符串会从请求体中省略。
         *
         * @param nickname 新昵称（空字符串 → 省略）
         * @param avatar 新头像 URL（空字符串 → 省略）
         * @param ctx 上下文 QObject，销毁时取消回调
         * @param cb 服务器响应回调
         */
        virtual auto UpdateProfile(
            const QString& nickname,
            const QString& avatar,
            QObject* ctx,
            UserApiCallback cb
        ) -> void;

        /**
         * @brief PUT /api/user/password — 修改已认证用户的密码
         *
         * @param oldPassword 当前密码（用于验证）
         * @param newPassword 新密码（8-64 字符，需包含大小写字母和数字）
         * @param ctx 上下文 QObject，销毁时取消回调
         * @param cb 服务器响应回调
         */
        virtual auto ChangePassword(
            const QString& oldPassword,
            const QString& newPassword,
            QObject* ctx,
            UserApiCallback cb
        ) -> void;

        virtual ~UserApi() = default;

    private:
        ApiClient* m_client;
    };

} // namespace disk::qml::api
