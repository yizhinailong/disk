#pragma once

#include <QMap>
#include <QNetworkReply>
#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <QVariant>
#include <memory>
#include <optional>

#include "models/DriveListModel.hpp"
#include "models/FolderTreeModel.hpp"
#include "network/ErrorAdapter.hpp"
#include "network/NetworkClient.hpp"
#include "network/RequestFactory.hpp"

namespace disk::desktop::managers {

    class DriveManager : public QObject {
        Q_OBJECT
        Q_PROPERTY(disk::desktop::DriveListModel* listModel READ listModel CONSTANT)
        Q_PROPERTY(disk::desktop::FolderTreeModel* treeModel READ treeModel CONSTANT)

    public:
        explicit DriveManager(
            disk::desktop::NetworkClient* networkClient,
            disk::desktop::RequestFactory* requestFactory,
            QObject* parent = nullptr
        );
        ~DriveManager() override;

        disk::desktop::DriveListModel* listModel() const;
        disk::desktop::FolderTreeModel* treeModel() const;

        Q_INVOKABLE void listFiles(const QString& parentId, int page = 1, int pageSize = 50, const QString& sort = "name_asc", const QString& typeFilter = "");
        Q_INVOKABLE void searchFiles(const QString& query);
        Q_INVOKABLE void createFolder(const QString& parentId, const QString& name);
        Q_INVOKABLE void renameItem(const QString& fileId, const QString& newName);
        Q_INVOKABLE void deleteItems(const QStringList& fileIds);
        Q_INVOKABLE void deleteDriveItems(const QVariantList& fileIds, const QVariantList& folderIds);
        Q_INVOKABLE void loadFolderTree();
        Q_INVOKABLE void loadBreadcrumb(const QString& folderId);
        Q_INVOKABLE void getFileDetail(const QString& fileId);

    signals:
        void apiError(const QString& message, int code);
        void fileDetailLoaded(const QVariantMap& detail);
        void breadcrumbLoaded(const QVariantList& breadcrumb);
        void operationSuccess(const QString& message);
        void paginationLoaded(int page, int totalPages, int total);
        void listLoadFailed(const QString& message, int code);
        void treeLoaded();

    private:
        auto PrepareHeaders() -> QMap<QString, QString>;
        static auto ParseJsonResponse(QNetworkReply* reply) -> std::optional<QJsonObject>;
        static auto BuildApiError(QNetworkReply* reply) -> disk::desktop::ApiError;
        void HandleListResponse(QNetworkReply* reply);
        void HandleSearchResponse(QNetworkReply* reply);
        void HandleDetailResponse(QNetworkReply* reply);
        void HandleCreateFolderResponse(QNetworkReply* reply);
        void HandleRenameResponse(QNetworkReply* reply);
        void HandleMoveResponse(QNetworkReply* reply);
        void HandleCopyResponse(QNetworkReply* reply);
        void HandleDeleteResponse(QNetworkReply* reply);
        void HandleTreeResponse(QNetworkReply* reply);
        void HandleBreadcrumbResponse(QNetworkReply* reply);
        void EmitApiError(QNetworkReply* reply);

        disk::desktop::DriveListModel* m_listModel;
        disk::desktop::FolderTreeModel* m_treeModel;
        disk::desktop::NetworkClient* m_networkClient;
        disk::desktop::RequestFactory* m_requestFactory;
        QVector<QNetworkReply*> m_active_replies;
    };

} // namespace disk::desktop::managers
