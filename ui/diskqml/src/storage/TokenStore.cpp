#include "TokenStore.hpp"

#include <QSettings>

namespace disk::qml::storage {

    auto TokenStore::Save(const QString& access, const QString& refresh, int expiresInSeconds) -> void {
        const QDateTime expiresAt = QDateTime::currentDateTimeUtc().addSecs(expiresInSeconds);

        QSettings settings;
        settings.beginGroup("auth");
        settings.setValue("accessToken", access);
        settings.setValue("refreshToken", refresh);
        settings.setValue("expiresAt", expiresAt);
        settings.endGroup();
    }

    auto TokenStore::Clear() -> void {
        QSettings settings;
        settings.beginGroup("auth");
        settings.remove("accessToken");
        settings.remove("refreshToken");
        settings.remove("expiresAt");
        settings.endGroup();
    }

    auto TokenStore::AccessToken() const -> QString {
        QSettings settings;
        settings.beginGroup("auth");
        const QString token = settings.value("accessToken").toString();
        settings.endGroup();
        return token;
    }

    auto TokenStore::RefreshToken() const -> QString {
        QSettings settings;
        settings.beginGroup("auth");
        const QString token = settings.value("refreshToken").toString();
        settings.endGroup();
        return token;
    }

    auto TokenStore::ExpiresAt() const -> QDateTime {
        QSettings settings;
        settings.beginGroup("auth");
        const QDateTime dt = settings.value("expiresAt").toDateTime();
        settings.endGroup();
        return dt;
    }

    auto TokenStore::HasValidAccessToken(int skewSeconds) const -> bool {
        const QString token = AccessToken();
        if (token.isEmpty()) {
            return false;
        }
        const QDateTime expiresAt = ExpiresAt();
        if (!expiresAt.isValid()) {
            return false;
        }
        return QDateTime::currentDateTimeUtc().addSecs(skewSeconds) < expiresAt;
    }

} // namespace disk::qml::storage
