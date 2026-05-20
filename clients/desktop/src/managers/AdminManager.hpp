/**
 * @file AdminManager.hpp
 * @brief Manager for admin API endpoints: user management, share management, system monitoring
 *
 * @copyright Copyright (c) 2026
 */

#pragma once

#include <QMap>
#include <QNetworkReply>
#include <QObject>
#include <QString>
#include <QVariantMap>
#include <memory>
#include <optional>

#include "models/AdminShareListModel.hpp"
#include "models/AdminUserListModel.hpp"
#include "models/OperationLogListModel.hpp"
#include "network/ErrorAdapter.hpp"
#include "network/NetworkClient.hpp"
#include "network/RequestFactory.hpp"

namespace disk::desktop::managers {

    class AdminManager : public QObject {
        Q_OBJECT
        Q_PROPERTY(AdminUserListModel* userModel READ GetUserModel CONSTANT)
        Q_PROPERTY(AdminShareListModel* shareModel READ GetShareModel CONSTANT)
        Q_PROPERTY(OperationLogListModel* operationLogModel READ GetOperationLogModel CONSTANT)
        Q_PROPERTY(QVariantMap overviewStats READ GetOverviewStats NOTIFY overviewStatsChanged)
        Q_PROPERTY(QVariantMap systemStatus READ GetSystemStatus NOTIFY systemStatusChanged)
        Q_PROPERTY(QVariantMap globalStorageStats READ GetGlobalStorageStatsMap NOTIFY globalStorageStatsChanged)
        Q_PROPERTY(QVariantMap systemInfo READ GetSystemInfoMap NOTIFY systemInfoChanged)

    public:
        explicit AdminManager(
            NetworkClient* network_client,
            RequestFactory* request_factory,
            QObject* parent = nullptr
        );
        ~AdminManager() override;

        auto GetUserModel() const -> AdminUserListModel*;
        auto GetShareModel() const -> AdminShareListModel*;
        auto GetOperationLogModel() const -> OperationLogListModel*;
        auto GetOverviewStats() const -> QVariantMap;
        auto GetSystemStatus() const -> QVariantMap;
        auto GetGlobalStorageStatsMap() const -> QVariantMap;
        auto GetSystemInfoMap() const -> QVariantMap;

        // User management (8 endpoints)
        Q_INVOKABLE void ListUsers(
            int page = 1,
            int pageSize = 20,
            const QString& username = "",
            const QString& email = "",
            int status = -1,
            int role = -1
        );
        Q_INVOKABLE void GetUserDetail(int userId);
        Q_INVOKABLE void ChangeUserStatus(int userId, int status);
        Q_INVOKABLE void ChangeUserRole(int userId, int role);
        Q_INVOKABLE void SoftDeleteUser(int userId);
        Q_INVOKABLE void GetGlobalStorageStats();

        // Share management (3 endpoints)
        Q_INVOKABLE void ListShares(
            int page = 1,
            int pageSize = 20,
            int status = -1,
            int userId = -1,
            const QString& username = ""
        );
        Q_INVOKABLE void GetShareDetail(int shareId);
        Q_INVOKABLE void ForceCancelShare(int shareId);

        // System monitoring (2 endpoints)
        Q_INVOKABLE void GetOverviewStatsApi();
        Q_INVOKABLE void GetSystemStatusApi();
        Q_INVOKABLE void ListOperationLogs(int page = 1, int pageSize = 20);
        Q_INVOKABLE void GetSystemInfo();

    signals:
        void userPaginationLoaded(int page, int totalPages, int total);
        void userDetailLoaded(const QVariantMap& detail);
        void userStorageLoaded(const QVariantMap& storage);
        void sharePaginationLoaded(int page, int totalPages, int total);
        void shareDetailLoaded(const QVariantMap& detail);
        void operationLogPaginationLoaded(int page, int totalPages, int total);
        void operationSuccess(const QString& message);
        void apiError(const QString& message, int code);
        void overviewStatsChanged();
        void systemStatusChanged();
        void globalStorageStatsChanged();
        void systemInfoChanged();

    private:
        auto PrepareHeaders() -> QMap<QString, QString>;
        static auto ParseJsonResponse(QNetworkReply* reply) -> std::optional<QJsonObject>;
        auto BuildApiError(QNetworkReply* reply) -> ApiError;
        void EmitApiError(QNetworkReply* reply);

        // Handler methods for each API response
        void HandleListUsersResponse(QNetworkReply* reply);
        void HandleGetUserDetailResponse(QNetworkReply* reply);
        void HandleChangeUserStatusResponse(QNetworkReply* reply);
        void HandleChangeUserRoleResponse(QNetworkReply* reply);
        void HandleSoftDeleteUserResponse(QNetworkReply* reply);
        void HandleGetGlobalStorageResponse(QNetworkReply* reply);
        void HandleListSharesResponse(QNetworkReply* reply);
        void HandleGetShareDetailResponse(QNetworkReply* reply);
        void HandleForceCancelShareResponse(QNetworkReply* reply);
        void HandleGetOverviewStatsResponse(QNetworkReply* reply);
        void HandleGetSystemStatusResponse(QNetworkReply* reply);
        void HandleListOperationLogsResponse(QNetworkReply* reply);
        void HandleGetSystemInfoResponse(QNetworkReply* reply);

        AdminUserListModel* m_user_model;
        AdminShareListModel* m_share_model;
        OperationLogListModel* m_operation_log_model;
        QVariantMap m_overview_stats;
        QVariantMap m_system_status;
        QVariantMap m_global_storage_stats;
        QVariantMap m_system_info;
        NetworkClient* m_network_client;
        RequestFactory* m_request_factory;
        QVector<QNetworkReply*> m_active_replies;
    };

} // namespace disk::desktop::managers
