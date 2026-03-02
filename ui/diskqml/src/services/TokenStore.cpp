#include "TokenStore.hpp"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>
#include <QTimeZone>

namespace disk::qml::services {

    static constexpr int kJsonVersion = 1;
    static const QString kFileName = QStringLiteral("token.json");

    TokenStore::TokenStore(const QString& baseDir) {
        // Resolve base directory
        if (!baseDir.isEmpty()) {
            m_base_dir = baseDir;
        } else {
            // Primary: ~/.cache/disk-ui
            const QString home = QDir::homePath();
            if (!home.isEmpty()) {
                m_base_dir = home + QStringLiteral("/.cache/disk-ui");
            } else {
                // Fallback: QStandardPaths home
                const QString spHome =
                    QStandardPaths::writableLocation(QStandardPaths::HomeLocation);
                if (!spHome.isEmpty()) {
                    m_base_dir = spHome + QStringLiteral("/.cache/disk-ui");
                } else {
                    // Last resort: current directory
                    m_base_dir = QDir::currentPath() + QStringLiteral("/.cache/disk-ui");
                }
            }
        }

        // One-time migration from legacy QSettings
        MigrateFromQSettings();
    }

    auto TokenStore::FilePath() const -> QString {
        return m_base_dir + QStringLiteral("/") + kFileName;
    }

    auto TokenStore::MigrateFromQSettings() -> void {
        QSettings settings;
        settings.beginGroup(QStringLiteral("auth"));

        const QString access = settings.value(QStringLiteral("accessToken")).toString();
        const QString refresh = settings.value(QStringLiteral("refreshToken")).toString();
        const QDateTime expiresAt = settings.value(QStringLiteral("expiresAt")).toDateTime();

        settings.endGroup();

        // Nothing to migrate
        // Already have a JSON file — skip migration entirely
        if (QFile::exists(FilePath())) {
            return;
        }

        // Only migrate if access token is non-empty AND expiresAt is valid
        if (access.isEmpty() || !expiresAt.isValid()) {
            return;
        }

        // Write migrated data to JSON
        QDir().mkpath(m_base_dir);

        QJsonObject obj;
        obj[QStringLiteral("version")] = kJsonVersion;
        obj[QStringLiteral("accessToken")] = access;
        obj[QStringLiteral("refreshToken")] = refresh;
        obj[QStringLiteral("expiresAtEpochMs")] = expiresAt.isValid() ? expiresAt.toMSecsSinceEpoch() : 0;

        QSaveFile file(FilePath());
        if (file.open(QIODevice::WriteOnly)) {
            file.write(QJsonDocument(obj).toJson(QJsonDocument::Compact));
            if (file.commit()) {
                // Best-effort permissions
                QFile::setPermissions(FilePath(), QFile::ReadOwner | QFile::WriteOwner);
                // Clear legacy keys only after successful write
                settings.beginGroup(QStringLiteral("auth"));
                settings.remove(QString{});
                settings.endGroup();
            }
        }
    }

    auto TokenStore::Save(const QString& access, const QString& refresh, int expiresInSeconds) -> void {
        const QDateTime expiresAt = QDateTime::currentDateTimeUtc().addSecs(expiresInSeconds);

        QDir().mkpath(m_base_dir);

        QJsonObject obj;
        obj[QStringLiteral("version")] = kJsonVersion;
        obj[QStringLiteral("accessToken")] = access;
        obj[QStringLiteral("refreshToken")] = refresh;
        obj[QStringLiteral("expiresAtEpochMs")] = expiresAt.toMSecsSinceEpoch();

        QSaveFile file(FilePath());
        if (file.open(QIODevice::WriteOnly)) {
            file.write(QJsonDocument(obj).toJson(QJsonDocument::Compact));
            file.commit();
            // Best-effort permissions
            QFile::setPermissions(FilePath(), QFile::ReadOwner | QFile::WriteOwner);
        }
    }

    auto TokenStore::Clear() -> void {
        QFile::remove(FilePath());
    }

    auto TokenStore::AccessToken() const -> QString {
        QFile file(FilePath());
        if (!file.open(QIODevice::ReadOnly)) {
            return {};
        }

        QJsonParseError err;
        const auto doc = QJsonDocument::fromJson(file.readAll(), &err);
        file.close();

        if (err.error != QJsonParseError::NoError || !doc.isObject()) {
            // Corruption: delete and treat as logged-out
            QFile::remove(FilePath());
            return {};
        }

        const QJsonObject obj = doc.object();
        if (!obj.value(QStringLiteral("accessToken")).isString() ||
            !obj.value(QStringLiteral("refreshToken")).isString() ||
            !obj.value(QStringLiteral("expiresAtEpochMs")).isDouble()) {
            QFile::remove(FilePath());
            return {};
        }

        return obj.value(QStringLiteral("accessToken")).toString();
    }

    auto TokenStore::RefreshToken() const -> QString {
        QFile file(FilePath());
        if (!file.open(QIODevice::ReadOnly)) {
            return {};
        }

        QJsonParseError err;
        const auto doc = QJsonDocument::fromJson(file.readAll(), &err);
        file.close();

        if (err.error != QJsonParseError::NoError || !doc.isObject()) {
            QFile::remove(FilePath());
            return {};
        }

        const QJsonObject obj = doc.object();
        if (!obj.value(QStringLiteral("accessToken")).isString() ||
            !obj.value(QStringLiteral("refreshToken")).isString() ||
            !obj.value(QStringLiteral("expiresAtEpochMs")).isDouble()) {
            QFile::remove(FilePath());
            return {};
        }

        return obj.value(QStringLiteral("refreshToken")).toString();
    }

    auto TokenStore::ExpiresAt() const -> QDateTime {
        QFile file(FilePath());
        if (!file.open(QIODevice::ReadOnly)) {
            return {};
        }

        QJsonParseError err;
        const auto doc = QJsonDocument::fromJson(file.readAll(), &err);
        file.close();

        if (err.error != QJsonParseError::NoError || !doc.isObject()) {
            QFile::remove(FilePath());
            return {};
        }

        const QJsonObject obj = doc.object();
        if (!obj.value(QStringLiteral("accessToken")).isString() ||
            !obj.value(QStringLiteral("refreshToken")).isString() ||
            !obj.value(QStringLiteral("expiresAtEpochMs")).isDouble()) {
            QFile::remove(FilePath());
            return {};
        }

        const qint64 ms = obj.value(QStringLiteral("expiresAtEpochMs")).toInteger(0);
        if (ms == 0) {
            return {};
        }
        return QDateTime::fromMSecsSinceEpoch(ms, QTimeZone::UTC);
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

} // namespace disk::qml::services
