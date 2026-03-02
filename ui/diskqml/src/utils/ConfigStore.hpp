#pragma once

#include <QSettings>
#include <QUrl>

namespace disk::qml::utils {

    class ConfigStore {
    public:
        ConfigStore();

        auto ServerUrl() const -> QUrl;
        auto SetServerUrl(const QUrl& url) -> void;

    private:
        mutable QSettings m_settings;
    };

} // namespace disk::qml::utils
