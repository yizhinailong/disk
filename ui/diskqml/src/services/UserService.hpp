/**
 * @file UserService.hpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief QML 客户端高级用户编排服务
 *
 * @copyright Copyright (c) 2026
 *
 */

#pragma once

#include <QObject>
#include <QString>
#include <functional>
#include <optional>

#include <dtos/UserDtos.hpp>

namespace disk::qml::api {
    class UserApi;
}

namespace disk::qml::services {

    class TokenRefreshCoordinator;

    /**
     * @brief QML 客户端用户服务。
     * @details 编排用户相关业务流程：
     *   - 获取已认证用户的资料
     *   - 更新用户资料（昵称、头像）
     *   - 修改用户密码
     *   - 获取存储使用量统计
     *   - 委托 api::UserApi 执行网络请求
     *   - 将传输层和响应封装层错误映射为用户友好消息
     */
    class UserService final {
    public:
        using ProfileCallback = std::function<void(std::optional<models::UserProfileDto> result, QString errorMessage)>;
        using StorageCallback = std::function<void(std::optional<models::StorageDto> result, QString errorMessage)>;
        using UpdateProfileCallback = std::function<void(std::optional<models::UpdateProfileResultDto> result, QString errorMessage)>;
        using ChangePasswordCallback = std::function<void(std::optional<models::ChangePasswordResultDto> result, QString errorMessage)>;

        explicit UserService(api::UserApi* userApi, TokenRefreshCoordinator* coordinator = nullptr);

        /**
         * @brief 获取存储使用量统计。
         * @details 返回已用空间、配额、文件/文件夹数量及分类细分。
         * @param ctx       QObject 生命周期守护。
         * @param cb        接收 (result, errorMessage)。成功时 errorMessage 为空。
         */
        auto GetProfile(QObject* ctx, ProfileCallback cb) -> void;

        /**
         * @brief 获取已认证用户的资料。
         * @details 在成功登录后调用以填充用户信息。
         * @param ctx       QObject 生命周期守护。
         * @param cb        接收 (result, errorMessage)。成功时 errorMessage 为空。
         */
        auto GetStorage(QObject* ctx, StorageCallback cb) -> void;

        /**
         * @brief 更新已认证用户的资料。
         * @details @p nickname 和 @p avatar 至少有一个非空。
         *   空字符串会从请求体中省略。
         * @param nickname  新昵称（空字符串 → 省略）
         * @param avatar    新头像 URL（空字符串 → 省略）
         * @param ctx       QObject 生命周期守护。
         * @param cb        接收 (result, errorMessage)。成功时 errorMessage 为空。
         */
        auto UpdateProfile(const QString& nickname, const QString& avatar, QObject* ctx, UpdateProfileCallback cb) -> void;

        /**
         * @brief 修改已认证用户的密码。
         * @param oldPassword  当前密码（用于验证）
         * @param newPassword  新密码（8-64 字符，需包含大小写字母和数字）
         * @param ctx          QObject 生命周期守护。
         * @param cb           接收 (result, errorMessage)。成功时 errorMessage 为空。
         */
        auto ChangePassword(const QString& oldPassword, const QString& newPassword, QObject* ctx, ChangePasswordCallback cb) -> void;

    private:
        auto MapTransportError(const QString& networkError) const -> QString;
        auto MapEnvelopeError(const models::ApiEnvelope& envelope) const -> QString;
        auto ValidatePassword(const QString& password) const -> bool;

        api::UserApi* m_user_api;
        TokenRefreshCoordinator* m_coordinator{ nullptr };
    };

} // namespace disk::qml::services
