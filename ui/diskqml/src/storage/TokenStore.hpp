#pragma once

#include <QDateTime>
#include <QDir>
#include <QStandardPaths>
#include <QString>

namespace disk::qml::storage {

    class TokenStore {
    public:
        explicit TokenStore(const QString& baseDir = QString{});

        auto Save(const QString& access, const QString& refresh, int expiresInSeconds) -> void;
        auto Clear() -> void;

        auto AccessToken() const -> QString;
        auto RefreshToken() const -> QString;
        auto ExpiresAt() const -> QDateTime;
        auto HasValidAccessToken(int skewSeconds = 30) const -> bool;

    private:
        auto FilePath() const -> QString;
        auto MigrateFromQSettings() -> void;

        QString m_base_dir;
    };

} // namespace disk::qml::storage
