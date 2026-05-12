#pragma once

#include <QMap>
#include <QNetworkReply>
#include <QObject>
#include <QString>
#include <memory>
#include <optional>

#include "models/BatchResultModel.hpp"
#include "models/TrashListModel.hpp"
#include "network/ErrorAdapter.hpp"
#include "network/NetworkClient.hpp"
#include "network/RequestFactory.hpp"

namespace disk::desktop::managers {

    class TrashManager : public QObject {
        Q_OBJECT
        Q_PROPERTY(disk::desktop::TrashListModel* listModel READ listModel CONSTANT)
        Q_PROPERTY(disk::desktop::BatchResultModel* batchResultModel READ batchResultModel CONSTANT)

    public:
        explicit TrashManager(
            disk::desktop::NetworkClient* networkClient,
            disk::desktop::RequestFactory* requestFactory,
            QObject* parent = nullptr
        );
        ~TrashManager() override;

        disk::desktop::TrashListModel* listModel() const;
        disk::desktop::BatchResultModel* batchResultModel() const;

        Q_INVOKABLE void listTrash(int page = 1, int pageSize = 20);
        Q_INVOKABLE void restoreItems(const QStringList& trashIds);
        Q_INVOKABLE void deleteItems(const QStringList& trashIds);
        Q_INVOKABLE void clearAll();

    signals:
        void apiError(const QString& message, int code);
        void operationSuccess(const QString& message);
        void paginationLoaded(int page, int totalPages, int total);
        void batchResultReady();
        void clearAllCompleted(int deletedCount, quint64 freedSpace);

    private:
        auto PrepareHeaders() -> QMap<QString, QString>;
        static auto ParseJsonResponse(QNetworkReply* reply) -> std::optional<QJsonObject>;
        void EmitApiError(QNetworkReply* reply);

        void HandleListResponse(QNetworkReply* reply);
        void HandleRestoreResponse(QNetworkReply* reply);
        void HandleDeleteResponse(QNetworkReply* reply);
        void HandleClearAllResponse(QNetworkReply* reply);

        disk::desktop::TrashListModel* m_listModel;
        disk::desktop::BatchResultModel* m_batchResultModel;
        disk::desktop::NetworkClient* m_networkClient;
        disk::desktop::RequestFactory* m_requestFactory;
        QVector<QNetworkReply*> m_active_replies;
    };

} // namespace disk::desktop::managers
