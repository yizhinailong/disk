/**
 * @file FormatUtils.hpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief QML singleton providing centralized formatting utilities
 *
 * @copyright Copyright (c) 2026
 *
 */

#pragma once

#include <QObject>
#include <QString>

#include <QtQml/qjsengine.h>
#include <QtQml/qqmlengine.h>
#include <QtQml/qqmlregistration.h>

namespace disk::qml::utils {

    /**
     * @brief QML singleton that provides centralized formatting utilities.
     *
     * @details
     * This utility class centralizes common formatting operations used across
     * multiple QML views, including file size formatting, date formatting,
     * file icon/label mapping, and permission/status label mapping.
     *
     * Following project convention: "QML/JavaScript ONLY handles UI rendering.
     * All business logic, API calls, and data processing MUST be in C++"
     *
     * Singleton lifecycle: an application-owned instance must be created and
     * registered with SetInstance() before the QML engine calls create().
     */
    class FormatUtils : public QObject {
        Q_OBJECT
        QML_ELEMENT
        QML_SINGLETON

    public:
        explicit FormatUtils(QObject* parent = nullptr);

        // ==================== Public API ====================

        /**
         * @brief Register the pre-created instance for use by the QML engine.
         *
         * @details
         * Must be called before the QML engine requests the singleton via create().
         * Ownership of @p instance remains with the caller (C++ side).
         */
        static auto SetInstance(FormatUtils* instance) -> void;

        /**
         * @brief QML singleton factory — called once by the QML engine.
         *
         * @details
         * Constraints enforced at runtime:
         * - s_instance must have been set via SetInstance() beforehand.
         * - @p qmlEngine must share thread affinity with the instance.
         * - Only a single QJSEngine may access this singleton; a second engine
         *   triggers a Q_ASSERT failure.
         * - Ownership is set to CppOwnership to prevent the engine from deleting
         *   the instance when the engine is torn down.
         */
        static auto create(QQmlEngine* qmlEngine, QJSEngine* jsEngine) -> FormatUtils*;

        // ==================== Formatting Methods ====================

        /**
         * @brief Format byte count to human-readable string (e.g., "1.23 GB").
         * @param bytes Byte count to format
         * @return Formatted string
         */
        Q_INVOKABLE static QString formatSize(qint64 bytes);

        /**
         * @brief Format ISO date string to display format (YYYY-MM-DD).
         * @param dateStr ISO date string (e.g., "2026-02-15T10:30:00Z")
         * @return Formatted date string (e.g., "2026-02-15") or "-" if empty
         */
        Q_INVOKABLE static QString formatDate(const QString& dateStr);

        /**
         * @brief Get emoji icon for file type.
         * @param fileType File type ("folder", "file")
         * @param mimeType MIME type (e.g., "image/png", "application/pdf")
         * @return Emoji icon string
         */
        Q_INVOKABLE static QString fileIcon(const QString& fileType, const QString& mimeType);

        /**
         * @brief Get localized label for file type.
         * @param fileType File type ("folder", "file")
         * @param mimeType MIME type (e.g., "image/png", "application/pdf")
         * @return Localized type label (e.g., "文件夹", "图片", "PDF")
         */
        Q_INVOKABLE static QString fileTypeLabel(const QString& fileType, const QString& mimeType);

        /**
         * @brief Get localized label for share permission.
         * @param permission Permission type ("view", "download")
         * @return Localized permission label (e.g., "仅查看", "可下载")
         */
        Q_INVOKABLE static QString permissionLabel(const QString& permission);

        /**
         * @brief Get localized label for share status.
         * @param status Status type ("active", "expired", "cancelled")
         * @return Localized status label (e.g., "有效", "已过期", "已取消")
         */
        Q_INVOKABLE static QString statusLabel(const QString& status);

    private:
        inline static FormatUtils* s_instance = nullptr;
        inline static QJSEngine* s_engine = nullptr;
    };

} // namespace disk::qml::utils
