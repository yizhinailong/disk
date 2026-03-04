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

} // namespace disk::qml::utils
