#pragma once

#include <QDateTime>
#include <QString>

namespace disk::qml::storage {

    class TokenStore {
    public:
        TokenStore() = default;

        auto Save(const QString& access, const QString& refresh, int expiresInSeconds) -> void;
        auto Clear() -> void;

        auto AccessToken() const -> QString;
        auto RefreshToken() const -> QString;
        auto ExpiresAt() const -> QDateTime;
        auto HasValidAccessToken(int skewSeconds = 30) const -> bool;
    };

} // namespace disk::qml::storage
