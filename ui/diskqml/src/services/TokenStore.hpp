/**
 * @file TokenStore.hpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief Local persistence for access/refresh tokens
 * @version 0.1
 * @date 2026-03-02
 *
 * @copyright Copyright (c) 2026
 *
 */
#pragma once

#include <QDateTime>
#include <QDir>
#include <QStandardPaths>
#include <QString>

namespace disk::qml::services {

    /**
     * @brief Persistent storage for JWT access and refresh tokens.
     * @details Tokens are serialised as JSON and written to
     *   `~/.cache/disk-ui/token.json` (owner read/write only).
     *   On first construction the store performs a one-time migration from the
     *   legacy QSettings-based storage; the QSettings keys are removed after a
     *   successful migration.
     */
    class TokenStore {
    public:
        /**
         * @brief Constructs the store and performs the legacy-QSettings migration.
         * @param baseDir  Override for the cache directory. When empty the default
         *   `~/.cache/disk-ui` path is used.
         */
        explicit TokenStore(const QString& baseDir = QString{});

        /**
         * @brief Saves tokens and the computed expiry time to disk.
         * @details Computes `expiresAt = now + expiresInSeconds` in UTC and writes
         *   an atomic JSON file (via QSaveFile) with 0600 permissions.
         * @param access           Opaque access token string.
         * @param refresh          Opaque refresh token string.
         * @param expiresInSeconds Lifetime of the access token in seconds.
         */
        auto Save(const QString& access, const QString& refresh, int expiresInSeconds) -> void;
        /**
         * @brief Removes the token file from disk, effectively logging the user out.
         */
        auto Clear() -> void;

        /**
         * @brief Returns the stored access token, or an empty string if unavailable.
         * @details Returns an empty string when the file is missing, unreadable,
         *   or structurally invalid (corrupt file is also deleted).
         */
        auto AccessToken() const -> QString;
        /**
         * @brief Returns the stored refresh token, or an empty string if unavailable.
         * @details Same corruption-detection semantics as AccessToken().
         */
        auto RefreshToken() const -> QString;
        /**
         * @brief Returns the UTC expiry time of the access token.
         * @return A valid QDateTime in UTC, or a null QDateTime if unavailable.
         */
        auto ExpiresAt() const -> QDateTime;
        /**
         * @brief Returns true if a non-empty access token exists and has not expired.
         * @details The check uses a clock-skew safety window: the token is considered
         *   valid only if `now + skewSeconds < expiresAt`. This prevents the caller
         *   from using a token that will expire within the next `skewSeconds` seconds.
         * @param skewSeconds  Clock-skew safety window in seconds (default: 30).
         * @return true if the token is present and its expiry is beyond now+skewSeconds.
         */
        auto HasValidAccessToken(int skewSeconds = 30) const -> bool;

    private:
        auto FilePath() const -> QString;
        auto MigrateFromQSettings() -> void;

        QString m_base_dir;
    };

} // namespace disk::qml::services
