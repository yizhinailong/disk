/**
 * @file UserService.cpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief UserService implementation
 *
 * @copyright Copyright (c) 2026
 *
 */

#include "UserService.hpp"

#include <QRegularExpression>

#include <api/UserApi.hpp>
#include <utils/ErrorCode.hpp>

namespace disk::qml::services {

    const QRegularExpression kPasswordPattern(QStringLiteral("^(?=.*[a-z])(?=.*[A-Z])(?=.*\\d)[a-zA-Z\\d]{8,64}$"));

    UserService::UserService(api::UserApi* userApi)
        : m_user_api(userApi) {
    }

    auto UserService::ValidatePassword(const QString& password) const -> bool {
        return kPasswordPattern.match(password).hasMatch();
    }

    auto UserService::GetProfile(QObject* ctx, ProfileCallback cb) -> void {
        m_user_api->GetProfile(
            ctx,
            [this, cb = std::move(cb)](models::ApiEnvelope envelope, QString networkError) {
                if (!networkError.isEmpty()) {
                    cb(std::nullopt, MapTransportError(networkError));
                    return;
                }

                if (envelope.code != static_cast<int>(utils::ErrorCode::Success)) {
                    cb(std::nullopt, MapEnvelopeError(envelope));
                    return;
                }

                auto parsed = models::ParseUserProfile(envelope.data);
                if (!parsed) {
                    cb(std::nullopt, QStringLiteral("服务器响应解析失败"));
                    return;
                }

                cb(std::move(parsed), QString{});
            }
        );
    }

    auto UserService::GetStorage(QObject* ctx, StorageCallback cb) -> void {
        m_user_api->GetStorage(
            ctx,
            [this, cb = std::move(cb)](models::ApiEnvelope envelope, QString networkError) {
                if (!networkError.isEmpty()) {
                    cb(std::nullopt, MapTransportError(networkError));
                    return;
                }

                if (envelope.code != static_cast<int>(utils::ErrorCode::Success)) {
                    cb(std::nullopt, MapEnvelopeError(envelope));
                    return;
                }

                auto parsed = models::ParseStorage(envelope.data);
                if (!parsed) {
                    cb(std::nullopt, QStringLiteral("服务器响应解析失败"));
                    return;
                }

                cb(std::move(parsed), QString{});
            }
        );
    }

    auto UserService::UpdateProfile(
        const QString& nickname,
        const QString& avatar,
        QObject* ctx,
        UpdateProfileCallback cb
    ) -> void {
        // Validate: at least one field must be provided
        if (nickname.trimmed().isEmpty() && avatar.trimmed().isEmpty()) {
            cb(
                std::nullopt,
                utils::ToUserMessage(static_cast<int>(utils::ErrorCode::InvalidParameter), QString{})
            );
            return;
        }

        m_user_api->UpdateProfile(
            nickname,
            avatar,
            ctx,
            [this, cb = std::move(cb)](models::ApiEnvelope envelope, QString networkError) {
                if (!networkError.isEmpty()) {
                    cb(std::nullopt, MapTransportError(networkError));
                    return;
                }

                if (envelope.code != static_cast<int>(utils::ErrorCode::Success)) {
                    cb(std::nullopt, MapEnvelopeError(envelope));
                    return;
                }

                auto parsed = models::ParseUpdateProfileResult(envelope.data);
                if (!parsed) {
                    cb(std::nullopt, QStringLiteral("服务器响应解析失败"));
                    return;
                }

                cb(std::move(parsed), QString{});
            }
        );
    }

    auto UserService::ChangePassword(
        const QString& oldPassword,
        const QString& newPassword,
        QObject* ctx,
        ChangePasswordCallback cb
    ) -> void {
        // Validate inputs
        if (oldPassword.isEmpty()) {
            cb(
                std::nullopt,
                utils::ToUserMessage(static_cast<int>(utils::ErrorCode::InvalidParameter), QString{})
            );
            return;
        }

        if (!ValidatePassword(newPassword)) {
            cb(
                std::nullopt,
                QStringLiteral("密码格式不正确：需8-64位，包含大小写字母和数字")
            );
            return;
        }

        m_user_api->ChangePassword(
            oldPassword,
            newPassword,
            ctx,
            [this, cb = std::move(cb)](models::ApiEnvelope envelope, QString networkError) {
                if (!networkError.isEmpty()) {
                    cb(std::nullopt, MapTransportError(networkError));
                    return;
                }

                if (envelope.code != static_cast<int>(utils::ErrorCode::Success)) {
                    cb(std::nullopt, MapEnvelopeError(envelope));
                    return;
                }

                auto parsed = models::ParseChangePasswordResult(envelope.data);
                if (!parsed) {
                    cb(std::nullopt, QStringLiteral("服务器响应解析失败"));
                    return;
                }

                cb(std::move(parsed), QString{});
            }
        );
    }

    auto UserService::MapTransportError(const QString& networkError) const -> QString {
        if (!networkError.isEmpty()) {
            return networkError;
        }
        return QStringLiteral("网络连接失败，请检查网络");
    }

    auto UserService::MapEnvelopeError(const models::ApiEnvelope& envelope) const -> QString {
        return utils::ToUserMessage(envelope.code, envelope.message);
    }

} // namespace disk::qml::services
