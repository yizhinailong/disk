/**
 * @file TokenStore.hpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 访问/刷新令牌本地持久化存储
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
     * @brief JWT 访问令牌和刷新令牌持久化存储。
     * @details 令牌序列化为 JSON 并写入
     *   `~/.cache/disk-ui/token.json`（仅所有者可读写）。
     *   首次构造时执行从旧版 QSettings 存储的一次性迁移；
     *   迁移成功后删除 QSettings 键。
     */
    class TokenStore {
    public:
        /**
         * @brief 构造存储并执行旧版 QSettings 迁移。
         * @param baseDir  缓存目录覆盖。为空时使用默认
         *   `~/.cache/disk-ui` 路径。
         */
        explicit TokenStore(const QString& baseDir = QString{});

        /**
         * @brief 保存令牌和计算的过期时间到磁盘。
         * @details 计算 `expiresAt = now + expiresInSeconds`（UTC）并写入
         *   原子 JSON 文件（通过 QSaveFile），权限为 0600。
         * @param access           不透明访问令牌字符串。
         * @param refresh          不透明刷新令牌字符串。
         * @param expiresInSeconds 访问令牌有效期秒数。
         */
        auto Save(const QString& access, const QString& refresh, int expiresInSeconds) -> void;
        /**
         * @brief 从磁盘删除令牌文件，实际上使用户登出。
         */
        auto Clear() -> void;

        /**
         * @brief 返回存储的访问令牌，不可用时返回空字符串。
         * @details 文件缺失、不可读或结构无效时返回空字符串
         *   （损坏文件也会被删除）。
         */
        auto AccessToken() const -> QString;
        /**
         * @brief 返回存储的刷新令牌，不可用时返回空字符串。
         * @details 与 AccessToken() 相同的损坏检测语义。
         */
        auto RefreshToken() const -> QString;
        /**
         * @brief 返回访问令牌的 UTC 过期时间。
         * @return 有效的 UTC QDateTime，不可用时返回空 QDateTime。
         */
        auto ExpiresAt() const -> QDateTime;
        /**
         * @brief 存在非空访问令牌且未过期时返回 true。
         * @details 检查使用时钟偏差安全窗口：仅当
         *   `now + skewSeconds < expiresAt` 时令牌视为有效。
         *   这防止调用者使用将在 `skewSeconds` 秒内过期的令牌。
         * @param skewSeconds  时钟偏差安全窗口秒数（默认：30）。
         * @return 令牌存在且过期时间在 now+skewSeconds 之后返回 true。
         */
        auto HasValidAccessToken(int skewSeconds = 30) const -> bool;

    private:
        auto FilePath() const -> QString;
        auto MigrateFromQSettings() -> void;

        QString m_base_dir;
    };

} // namespace disk::qml::services
