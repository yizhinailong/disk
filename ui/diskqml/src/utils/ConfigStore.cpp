/**
 * @file ConfigStore.cpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief ConfigStore implementation
 * @version 0.1
 * @date 2026-03-02
 *
 * @copyright Copyright (c) 2026
 *
 */
#include "ConfigStore.hpp"

namespace disk::qml::utils {

    ConfigStore::ConfigStore() = default;

    auto ConfigStore::ServerUrl() const -> QUrl {
        m_settings.beginGroup("config");
        QUrl url = QUrl(m_settings.value("serverUrl", "http://127.0.0.1:8080").toString());
        m_settings.endGroup();
        return url;
    }

    auto ConfigStore::SetServerUrl(const QUrl& url) -> void {
        m_settings.beginGroup("config");
        m_settings.setValue("serverUrl", url.toString());
        m_settings.endGroup();
    }

    auto ConfigStore::ConcurrentUploads() const -> int {
        m_settings.beginGroup("transfers");
        const int val = m_settings.value("concurrentUploads", kDefaultConcurrentUploads).toInt();
        m_settings.endGroup();
        return qBound(1, val, 10);
    }

    auto ConfigStore::SetConcurrentUploads(int value) -> void {
        m_settings.beginGroup("transfers");
        m_settings.setValue("concurrentUploads", qBound(1, value, 10));
        m_settings.endGroup();
    }

    auto ConfigStore::ConcurrentDownloads() const -> int {
        m_settings.beginGroup("transfers");
        const int val = m_settings.value("concurrentDownloads", kDefaultConcurrentDownloads).toInt();
        m_settings.endGroup();
        return qBound(1, val, 10);
    }

    auto ConfigStore::SetConcurrentDownloads(int value) -> void {
        m_settings.beginGroup("transfers");
        m_settings.setValue("concurrentDownloads", qBound(1, value, 10));
        m_settings.endGroup();
    }

} // namespace disk::qml::utils
