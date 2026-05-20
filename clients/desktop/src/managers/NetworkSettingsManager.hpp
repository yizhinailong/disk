#pragma once

#include <QObject>
#include <QString>

namespace disk::desktop {
    class NetworkClient;
}

namespace disk::desktop::managers {

    class NetworkSettingsManager : public QObject {
        Q_OBJECT
        Q_PROPERTY(QString serverUrl READ serverUrl NOTIFY serverUrlChanged)
        Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY errorMessageChanged)

    public:
        explicit NetworkSettingsManager(
            disk::desktop::NetworkClient* networkClient,
            QObject* parent = nullptr
        );

        [[nodiscard]] QString serverUrl() const;
        [[nodiscard]] QString errorMessage() const;

        Q_INVOKABLE bool saveServerUrl(const QString& url);
        Q_INVOKABLE void resetServerUrl();

    signals:
        void serverUrlChanged();
        void errorMessageChanged();
        void operationSuccess(const QString& message);
        void operationFailed(const QString& message);

    private:
        static QString normalizeUrl(const QString& url);
        bool applyServerUrl(const QString& url, bool persist);
        void setErrorMessage(const QString& message);

        disk::desktop::NetworkClient* m_networkClient;
        QString m_serverUrl;
        QString m_errorMessage;
    };

} // namespace disk::desktop::managers
