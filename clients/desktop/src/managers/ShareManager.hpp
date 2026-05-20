#pragma once

#include <QMap>
#include <QNetworkReply>
#include <QObject>
#include <QString>
#include <QVariantMap>
#include <memory>
#include <optional>

#include "models/BatchResultModel.hpp"
#include "models/DriveListModel.hpp"
#include "models/ShareListModel.hpp"
#include "network/ErrorAdapter.hpp"
#include "network/NetworkClient.hpp"
#include "network/RequestFactory.hpp"

namespace disk::desktop::managers {

    class ShareManager : public QObject {
        Q_OBJECT
        Q_PROPERTY(disk::desktop::ShareListModel* listModel READ listModel CONSTANT)
        Q_PROPERTY(disk::desktop::DriveListModel* browseModel READ browseModel CONSTANT)
        Q_PROPERTY(disk::desktop::BatchResultModel* batchResultModel READ batchResultModel CONSTANT)

    public:
        explicit ShareManager(
            disk::desktop::NetworkClient* networkClient,
            disk::desktop::RequestFactory* requestFactory,
            QObject* parent = nullptr
        );
        ~ShareManager() override;

        disk::desktop::ShareListModel* listModel() const;
        disk::desktop::DriveListModel* browseModel() const;
        disk::desktop::BatchResultModel* batchResultModel() const;

        // Owner operations (JWT auth)
        Q_INVOKABLE void listShares(int page = 1, int pageSize = 20, const QString& status = "all");
        Q_INVOKABLE void createShare(
            const QStringList& fileIds,
            const QString& permission = "download",
            const QString& password = {},
            int expireDays = 7
        );
        Q_INVOKABLE void updateShare(
            const QString& shareId,
            const QString& permission = {},
            const QString& password = {},
            int expireDays = -1
        );
        Q_INVOKABLE void getShareDetail(const QString& shareId);
        Q_INVOKABLE void cancelShares(const QStringList& shareIds);

        // Visitor operations (Share Token auth)
        Q_INVOKABLE void browseShare(const QString& shareId, const QString& parentId = {});
        Q_INVOKABLE QString parseShareInput(const QString& input) const;

    signals:
        void apiError(const QString& message, int code);
        void operationSuccess(const QString& message);
        void shareDetailLoaded(const QVariantMap& detail);
        void paginationLoaded(int page, int totalPages, int total);
        void batchResultReady();
        void browseLoaded(const QString& shareId);
        void shareCreated(const QString& shareId, const QString& shareLink);

    private:
        auto PrepareOwnerHeaders() -> QMap<QString, QString>;
        auto PrepareVisitorHeaders() -> QMap<QString, QString>;
        static auto ParseJsonResponse(QNetworkReply* reply) -> std::optional<QJsonObject>;
        void EmitApiError(QNetworkReply* reply);

        void HandleListResponse(QNetworkReply* reply);
        void HandleCreateResponse(QNetworkReply* reply);
        void HandleDetailResponse(QNetworkReply* reply);
        void HandleUpdateResponse(QNetworkReply* reply);
        void HandleCancelResponse(QNetworkReply* reply);
        void HandleBrowseResponse(QNetworkReply* reply, const QString& shareId);
        void HandleDetailVisitorResponse(QNetworkReply* reply);

        disk::desktop::ShareListModel* m_listModel;
        disk::desktop::DriveListModel* m_browseModel;
        disk::desktop::BatchResultModel* m_batchResultModel;
        disk::desktop::NetworkClient* m_networkClient;
        disk::desktop::RequestFactory* m_requestFactory;
        QVector<QNetworkReply*> m_active_replies;
    };

} // namespace disk::desktop::managers
