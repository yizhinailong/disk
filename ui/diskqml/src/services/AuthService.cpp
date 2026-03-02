#include "AuthService.hpp"

#include <QRegularExpression>

#include <api/AuthApi.hpp>
#include <services/TokenStore.hpp>
#include <utils/ErrorCode.hpp>

namespace disk::qml::services {

    const QRegularExpression kUsernamePattern(QStringLiteral("^[a-zA-Z0-9_]{4,32}$"));
    const QRegularExpression kEmailPattern(QStringLiteral("^[a-zA-Z0-9._%+-]+@[a-zA-Z0-9.-]+\\.[a-zA-Z]{2,}$"));
    const QRegularExpression kPasswordPattern(QStringLiteral("^(?=.*[a-z])(?=.*[A-Z])(?=.*\\d)[a-zA-Z\\d]{8,64}$"));

    AuthService::AuthService(api::AuthApi* authApi, TokenStore* tokenStore)
        : m_auth_api(authApi), m_token_store(tokenStore) {
    }

    auto AuthService::ValidateUsername(const QString& username) const -> bool {
        return kUsernamePattern.match(username).hasMatch();
    }

    auto AuthService::ValidateEmail(const QString& email) const -> bool {
        return kEmailPattern.match(email).hasMatch();
    }

    auto AuthService::ValidatePassword(const QString& password) const -> bool {
        return kPasswordPattern.match(password).hasMatch();
    }

    auto AuthService::Register(
        const QString& username,
        const QString& email,
        const QString& password,
        QObject* ctx,
        RegisterCallback cb
    ) -> void {
        if (!ValidateUsername(username) || !ValidateEmail(email) || !ValidatePassword(password)) {
            cb(std::nullopt, utils::ToUserMessage(static_cast<int>(utils::ErrorCode::InvalidFormat), QString{}));
            return;
        }

        m_auth_api->Register(
            username,
            email,
            password,
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

                auto parsed = models::ParseRegisterResult(envelope.data);
                if (!parsed) {
                    cb(std::nullopt, QStringLiteral("服务器响应解析失败"));
                    return;
                }

                cb(std::move(parsed), QString{});
            }
        );
    }

    auto AuthService::Login(
        const QString& account,
        const QString& password,
        QObject* ctx,
        LoginCallback cb
    ) -> void {
        if (account.trimmed().isEmpty() || password.isEmpty()) {
            cb(
                std::nullopt,
                utils::ToUserMessage(static_cast<int>(utils::ErrorCode::InvalidParameter), QString{})
            );
            return;
        }

        m_auth_api->Login(
            account,
            password,
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

                auto parsed = models::ParseLoginResult(envelope.data);
                if (!parsed) {
                    cb(std::nullopt, QStringLiteral("服务器响应解析失败"));
                    return;
                }

                m_token_store->Save(parsed->accessToken, parsed->refreshToken, parsed->expiresIn);
                cb(std::move(parsed), QString{});
            }
        );
    }

    auto AuthService::Refresh(
        const QString& refreshToken,
        QObject* ctx,
        RefreshCallback cb
    ) -> void {
        if (refreshToken.trimmed().isEmpty()) {
            cb(
                std::nullopt,
                utils::ToUserMessage(static_cast<int>(utils::ErrorCode::InvalidParameter), QString{})
            );
            return;
        }

        m_auth_api->Refresh(
            refreshToken,
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

                auto parsed = models::ParseRefreshResult(envelope.data);
                if (!parsed) {
                    cb(std::nullopt, QStringLiteral("服务器响应解析失败"));
                    return;
                }

                m_token_store->Save(parsed->accessToken, parsed->refreshToken, parsed->expiresIn);
                cb(std::move(parsed), QString{});
            }
        );
    }

    auto AuthService::Logout(const QString& accessToken, QObject* ctx, LogoutCallback cb) -> void {
        if (accessToken.trimmed().isEmpty()) {
            m_token_store->Clear();
            cb(true, QString{});
            return;
        }

        m_auth_api->Logout(
            accessToken,
            ctx,
            [this, cb = std::move(cb)](models::ApiEnvelope envelope, QString networkError) {
                if (!networkError.isEmpty()) {
                    cb(false, MapTransportError(networkError));
                    return;
                }

                if (envelope.code == static_cast<int>(utils::ErrorCode::Success) ||
                    IsLocalLogoutSuccessCode(envelope.code)) {
                    m_token_store->Clear();
                    cb(true, QString{});
                    return;
                }

                cb(false, MapEnvelopeError(envelope));
            }
        );
    }

    auto AuthService::MapTransportError(const QString& networkError) const -> QString {
        // If we have a network error, treat it as already user-friendly and return as-is
        if (!networkError.isEmpty()) {
            return networkError;
        }
        // Generic fallback (shouldn't normally reach here)
        return QStringLiteral("网络连接失败，请检查网络");
    }

    auto AuthService::MapEnvelopeError(const models::ApiEnvelope& envelope) const -> QString {
        return utils::ToUserMessage(envelope.code, envelope.message);
    }

    auto AuthService::IsLocalLogoutSuccessCode(int code) const -> bool {
        switch (code) {
            case static_cast<int>(utils::ErrorCode::InvalidToken):
            case static_cast<int>(utils::ErrorCode::InvalidRefreshToken):
            case static_cast<int>(utils::ErrorCode::TokenMissing):
            case static_cast<int>(utils::ErrorCode::TokenMalformed):
            case static_cast<int>(utils::ErrorCode::TokenExpired):
            case static_cast<int>(utils::ErrorCode::RefreshTokenAlreadyUsed):
            case static_cast<int>(utils::ErrorCode::TokenRevoked):
                return true;
            default:
                return false;
        }
    }

} // namespace disk::qml::services
