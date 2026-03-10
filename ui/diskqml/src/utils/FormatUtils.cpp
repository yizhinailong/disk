/**
 * @file FormatUtils.cpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief Implementation of centralized formatting utilities
 *
 * @copyright Copyright (c) 2026
 *
 */

#include "FormatUtils.hpp"

#include <QJSEngine>
#include <QQmlEngine>
#include <QStringBuilder>
#include <QtGlobal>

namespace disk::qml::utils {

    FormatUtils::FormatUtils(QObject* parent)
        : QObject(parent) {}

    auto FormatUtils::SetInstance(FormatUtils* instance) -> void {
        s_instance = instance;
    }

    auto FormatUtils::create(QQmlEngine* qmlEngine, QJSEngine* jsEngine) -> FormatUtils* {
        Q_UNUSED(jsEngine)

        // Ensure instance was set via SetInstance() before QML engine access
        Q_ASSERT(s_instance != nullptr);

        // Ensure only one QML engine accesses this singleton
        if (s_engine != nullptr && s_engine != jsEngine) {
            Q_ASSERT_X(false, "FormatUtils::create", "Only one QJSEngine may access this singleton");
        }
        s_engine = jsEngine;

        // Set ownership to C++ side to prevent QML from deleting it
        QQmlEngine::setObjectOwnership(s_instance, QQmlEngine::CppOwnership);

        return s_instance;
    }

    auto FormatUtils::formatSize(qint64 bytes) -> QString {
        // Handle edge cases
        if (bytes <= 0) {
            return QStringLiteral("0 B");
        }

        // Use the same logic as SessionViewModel::FormatBytes
        constexpr double kKB = 1024.0;
        constexpr double kMB = kKB * 1024.0;
        constexpr double kGB = kMB * 1024.0;
        constexpr double kTB = kGB * 1024.0;

        const auto value = static_cast<double>(bytes);

        if (value >= kTB) {
            return QString::number(value / kTB, 'f', 2) % QStringLiteral(" TB");
        }
        if (value >= kGB) {
            return QString::number(value / kGB, 'f', 2) % QStringLiteral(" GB");
        }
        if (value >= kMB) {
            return QString::number(value / kMB, 'f', 2) % QStringLiteral(" MB");
        }
        if (value >= kKB) {
            return QString::number(value / kKB, 'f', 2) % QStringLiteral(" KB");
        }
        return QString::number(bytes) % QStringLiteral(" B");
    }

    auto FormatUtils::formatDate(const QString& dateStr) -> QString {
        if (dateStr.isEmpty()) {
            return QStringLiteral("-");
        }

        // Truncate to first 10 chars: "2026-02-15"
        // Input format: "2026-02-15T10:30:00Z" or "2026-02-15 10:30:00"
        return dateStr.left(10);
    }

    auto FormatUtils::fileIcon(const QString& fileType, const QString& mimeType) -> QString {
        // Check folder first
        if (fileType == QStringLiteral("folder")) {
            return QStringLiteral(u"\U0001F4C1"); // 📁
        }

        // Check by MIME type
        if (mimeType.startsWith(QStringLiteral("image/"))) {
            return QStringLiteral(u"\U0001F5BC"); // 🖼️
        }
        if (mimeType.startsWith(QStringLiteral("video/"))) {
            return QStringLiteral(u"\U0001F3AC"); // 🎦
        }
        if (mimeType.startsWith(QStringLiteral("audio/"))) {
            return QStringLiteral(u"\U0001F3B5"); // 🎵
        }
        if (mimeType == QStringLiteral("application/pdf")) {
            return QStringLiteral(u"\U0001F4D5"); // 📕
        }
        if (mimeType.indexOf(QStringLiteral("spreadsheet")) >= 0 ||
            mimeType.indexOf(QStringLiteral("excel")) >= 0 ||
            mimeType.indexOf(QStringLiteral("csv")) >= 0) {
            return QStringLiteral(u"\U0001F4CA"); // 📊
        }
        if (mimeType.indexOf(QStringLiteral("presentation")) >= 0 ||
            mimeType.indexOf(QStringLiteral("powerpoint")) >= 0) {
            return QStringLiteral(u"\U0001F3A5"); // 🎥️
        }
        if (mimeType.indexOf(QStringLiteral("word")) >= 0 ||
            mimeType.indexOf(QStringLiteral("document")) >= 0 ||
            mimeType.startsWith(QStringLiteral("text/"))) {
            return QStringLiteral(u"\U0001F4C4"); // 📄
        }
        if (mimeType.indexOf(QStringLiteral("zip")) >= 0 ||
            mimeType.indexOf(QStringLiteral("rar")) >= 0 ||
            mimeType.indexOf(QStringLiteral("tar")) >= 0 ||
            mimeType.indexOf(QStringLiteral("compress")) >= 0 ||
            mimeType.indexOf(QStringLiteral("7z")) >= 0) {
            return QStringLiteral(u"\U0001F4E6"); // 📦
        }
        if (mimeType.indexOf(QStringLiteral("javascript")) >= 0 ||
            mimeType.indexOf(QStringLiteral("json")) >= 0 ||
            mimeType.indexOf(QStringLiteral("xml")) >= 0 ||
            mimeType.indexOf(QStringLiteral("x-c")) >= 0 ||
            mimeType.indexOf(QStringLiteral("python")) >= 0) {
            return QStringLiteral(u"\U0001F4BB"); // 💻
        }

        // Default icon for unknown types
        return QStringLiteral(u"\U0001F4CE"); // 📎
    }

    auto FormatUtils::fileTypeLabel(const QString& fileType, const QString& mimeType) -> QString {
        // Check folder first
        if (fileType == QStringLiteral("folder")) {
            return QStringLiteral("文件夹");
        }

        // Check by MIME type
        if (mimeType.startsWith(QStringLiteral("image/"))) {
            return QStringLiteral("图片");
        }
        if (mimeType.startsWith(QStringLiteral("video/"))) {
            return QStringLiteral("视频");
        }
        if (mimeType.startsWith(QStringLiteral("audio/"))) {
            return QStringLiteral("音频");
        }
        if (mimeType == QStringLiteral("application/pdf")) {
            return QStringLiteral("PDF");
        }
        if (mimeType.indexOf(QStringLiteral("spreadsheet")) >= 0 ||
            mimeType.indexOf(QStringLiteral("excel")) >= 0) {
            return QStringLiteral("表格");
        }
        if (mimeType.indexOf(QStringLiteral("presentation")) >= 0 ||
            mimeType.indexOf(QStringLiteral("powerpoint")) >= 0) {
            return QStringLiteral("演示");
        }
        if (mimeType.indexOf(QStringLiteral("word")) >= 0 ||
            mimeType.indexOf(QStringLiteral("document")) >= 0) {
            return QStringLiteral("文档");
        }
        if (mimeType.startsWith(QStringLiteral("text/"))) {
            return QStringLiteral("文本");
        }
        if (mimeType.indexOf(QStringLiteral("zip")) >= 0 ||
            mimeType.indexOf(QStringLiteral("rar")) >= 0 ||
            mimeType.indexOf(QStringLiteral("compress")) >= 0) {
            return QStringLiteral("压缩包");
        }

        // Default label
        return QStringLiteral("文件");
    }

    auto FormatUtils::permissionLabel(const QString& permission) -> QString {
        if (permission == QStringLiteral("view")) {
            return QStringLiteral("仅查看");
        }
        if (permission == QStringLiteral("download")) {
            return QStringLiteral("可下载");
        }
        // Return as-is for unknown permissions
        return permission;
    }

    auto FormatUtils::statusLabel(const QString& status) -> QString {
        if (status == QStringLiteral("active")) {
            return QStringLiteral("有效");
        }
        if (status == QStringLiteral("expired")) {
            return QStringLiteral("已过期");
        }
        if (status == QStringLiteral("cancelled")) {
            return QStringLiteral("已取消");
        }
        // Return as-is for unknown statuses
        return status;
    }

} // namespace disk::qml::utils
