#include "managers/NetworkSettingsManager.hpp"

#include <QSettings>
#include <QUrl>

#include "network/NetworkClient.hpp"

namespace disk::desktop::managers {

    namespace {
        constexpr auto SETTINGS_KEY = "network/serverUrl";
        constexpr auto DEFAULT_SERVER_URL = "http://127.0.0.1:8080/";
    }

    NetworkSettingsManager::NetworkSettingsManager(
        disk::desktop::NetworkClient* networkClient,
        QObject* parent
    )
        : QObject(parent), m_networkClient(networkClient) {
        QSettings settings;
        auto configured_url = settings.value(SETTINGS_KEY, DEFAULT_SERVER_URL).toString();
        if (!applyServerUrl(configured_url, false)) {
            applyServerUrl(DEFAULT_SERVER_URL, true);
        }
    }

    QString NetworkSettingsManager::serverUrl() const {
        return m_serverUrl;
    }

    QString NetworkSettingsManager::errorMessage() const {
        return m_errorMessage;
    }

    bool NetworkSettingsManager::saveServerUrl(const QString& url) {
        if (!applyServerUrl(url, true)) {
            emit operationFailed(m_errorMessage);
            return false;
        }

        emit operationSuccess(QStringLiteral("服务器地址已保存"));
        return true;
    }

    void NetworkSettingsManager::resetServerUrl() {
        applyServerUrl(DEFAULT_SERVER_URL, true);
        emit operationSuccess(QStringLiteral("服务器地址已恢复默认"));
    }

    QString NetworkSettingsManager::normalizeUrl(const QString& url) {
        auto normalized = url.trimmed();
        if (!normalized.endsWith('/')) {
            normalized += '/';
        }
        return normalized;
    }

    bool NetworkSettingsManager::applyServerUrl(const QString& url, bool persist) {
        auto normalized = normalizeUrl(url);
        auto parsed = QUrl(normalized);
        if (!parsed.isValid() || parsed.scheme().isEmpty() || parsed.host().isEmpty() ||
            (parsed.scheme() != QStringLiteral("http") && parsed.scheme() != QStringLiteral("https"))) {
            setErrorMessage(QStringLiteral("请输入有效的 HTTP 或 HTTPS 服务器地址"));
            return false;
        }

        if (m_networkClient) {
            m_networkClient->SetBaseUrl(normalized);
            normalized = m_networkClient->GetBaseUrl();
        }

        if (persist) {
            QSettings settings;
            settings.setValue(SETTINGS_KEY, normalized);
        }

        if (m_serverUrl != normalized) {
            m_serverUrl = normalized;
            emit serverUrlChanged();
        }
        setErrorMessage(QString());
        return true;
    }

    void NetworkSettingsManager::setErrorMessage(const QString& message) {
        if (m_errorMessage == message) {
            return;
        }
        m_errorMessage = message;
        emit errorMessageChanged();
    }

} // namespace disk::desktop::managers
