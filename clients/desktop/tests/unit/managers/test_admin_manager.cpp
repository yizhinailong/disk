#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTest>
#include <QUrlQuery>

#include "helpers/MockNetworkAccessManager.hpp"
#include "helpers/TestJsonLoader.hpp"
#include "managers/AdminManager.hpp"
#include "network/NetworkClient.hpp"
#include "network/RequestFactory.hpp"

using namespace disk::desktop;
using namespace disk::desktop::managers;
using namespace disk::desktop::testing;

class TestAdminManager : public QObject {
    Q_OBJECT

private slots:

    void InitHasUserModelAndShareModel() {
        MockNetworkAccessManager mock_network;
        NetworkClient network_client(static_cast<QNetworkAccessManager*>(&mock_network));
        RequestFactory request_factory;
        AdminManager mgr(&network_client, &request_factory);

        QVERIFY(mgr.GetUserModel() != nullptr);
        QVERIFY(mgr.GetShareModel() != nullptr);
        QCOMPARE(mgr.GetUserModel()->rowCount(), 0);
        QCOMPARE(mgr.GetShareModel()->rowCount(), 0);
        QVERIFY(mgr.GetOperationLogModel() != nullptr);
        QCOMPARE(mgr.GetOperationLogModel()->rowCount(), 0);
        QVERIFY(mgr.GetOverviewStats().isEmpty());
        QVERIFY(mgr.GetSystemStatus().isEmpty());
        QVERIFY(mgr.GetGlobalStorageStatsMap().isEmpty());
        QVERIFY(mgr.GetSystemInfoMap().isEmpty());
    }

    // ── ListUsers ──

    void ListUsersPopulatesUserModel() {
        MockNetworkAccessManager mock_network;
        mock_network.RegisterResponse(
            "api/admin/users",
            TestJsonLoader::LoadJson("admin/admin_list_users_success.json")
        );

        NetworkClient network_client(static_cast<QNetworkAccessManager*>(&mock_network));
        network_client.SetBaseUrl("http://127.0.0.1:8080/");
        RequestFactory request_factory;
        request_factory.SetOwnerAccessToken("admin_token");
        AdminManager mgr(&network_client, &request_factory);

        QSignalSpy pagination_spy(&mgr, &AdminManager::userPaginationLoaded);
        QSignalSpy error_spy(&mgr, &AdminManager::apiError);

        mgr.ListUsers();

        QTRY_COMPARE(pagination_spy.count(), 1);
        QCOMPARE(error_spy.count(), 0);
        QCOMPARE(mgr.GetUserModel()->rowCount(), 3);

        QCOMPARE(
            mgr.GetUserModel()->data(mgr.GetUserModel()->index(0), AdminUserListModel::UsernameRole).toString(),
            QString("alice")
        );
        QCOMPARE(
            mgr.GetUserModel()->data(mgr.GetUserModel()->index(1), AdminUserListModel::UsernameRole).toString(),
            QString("bob")
        );
        QCOMPARE(
            mgr.GetUserModel()->data(mgr.GetUserModel()->index(2), AdminUserListModel::UsernameRole).toString(),
            QString("charlie")
        );

        auto args = pagination_spy.takeFirst();
        QCOMPARE(args.at(0).toInt(), 1);
        QCOMPARE(args.at(1).toInt(), 5);
        QCOMPARE(args.at(2).toInt(), 100);
    }

    void ListUsersHandlesApiError() {
        MockNetworkAccessManager mock_network;
        mock_network.RegisterResponse(
            "api/admin/users",
            TestJsonLoader::LoadJson("admin/admin_not_authorized.json"),
            403
        );

        NetworkClient network_client(static_cast<QNetworkAccessManager*>(&mock_network));
        network_client.SetBaseUrl("http://127.0.0.1:8080/");
        RequestFactory request_factory;
        request_factory.SetOwnerAccessToken("user_token");
        AdminManager mgr(&network_client, &request_factory);

        QSignalSpy pagination_spy(&mgr, &AdminManager::userPaginationLoaded);
        QSignalSpy error_spy(&mgr, &AdminManager::apiError);

        mgr.ListUsers();

        QTRY_COMPARE(error_spy.count(), 1);
        QCOMPARE(pagination_spy.count(), 0);

        auto args = error_spy.takeFirst();
        QCOMPARE(args.at(1).toInt(), 80001);
    }

    // ── GetUserDetail ──

    void GetUserDetailEmitsDetail() {
        MockNetworkAccessManager mock_network;
        mock_network.RegisterResponse(
            "api/admin/users/1",
            TestJsonLoader::LoadJson("admin/admin_get_user_detail_success.json")
        );

        NetworkClient network_client(static_cast<QNetworkAccessManager*>(&mock_network));
        network_client.SetBaseUrl("http://127.0.0.1:8080/");
        RequestFactory request_factory;
        request_factory.SetOwnerAccessToken("admin_token");
        AdminManager mgr(&network_client, &request_factory);

        QSignalSpy detail_spy(&mgr, &AdminManager::userDetailLoaded);
        QSignalSpy error_spy(&mgr, &AdminManager::apiError);

        mgr.GetUserDetail(1);

        QTRY_COMPARE(detail_spy.count(), 1);
        QCOMPARE(error_spy.count(), 0);

        auto detail = detail_spy.takeFirst().at(0).toMap();
        QCOMPARE(detail.value("id").toDouble(), 1.0);
        QCOMPARE(detail.value("username").toString(), QString("alice"));
        QCOMPARE(detail.value("email").toString(), QString("alice@example.com"));
        QCOMPARE(detail.value("nickname").toString(), QString("Alice"));
        QCOMPARE(detail.value("role").toInt(), 0);
        QCOMPARE(detail.value("status").toInt(), 1);
        QCOMPARE(detail.value("storage_quota").toDouble(), 10737418240.0);
        QCOMPARE(detail.value("storage_used").toDouble(), 1073741824.0);
        QCOMPARE(detail.value("storage_reserved").toDouble(), 536870912.0);
        QCOMPARE(detail.value("created_at").toString(), QString("2026-01-01T00:00:00Z"));
        QCOMPARE(detail.value("last_login_at").toString(), QString("2026-01-15T10:30:00Z"));
    }

    void GetUserDetailHandlesApiError() {
        MockNetworkAccessManager mock_network;
        mock_network.RegisterResponse(
            "api/admin/users/999",
            QJsonObject{
                { "code", 80002 },
                { "message", "用户不存在" },
                { "data", QJsonValue::Null },
            },
            404
        );

        NetworkClient network_client(static_cast<QNetworkAccessManager*>(&mock_network));
        network_client.SetBaseUrl("http://127.0.0.1:8080/");
        RequestFactory request_factory;
        request_factory.SetOwnerAccessToken("admin_token");
        AdminManager mgr(&network_client, &request_factory);

        QSignalSpy detail_spy(&mgr, &AdminManager::userDetailLoaded);
        QSignalSpy error_spy(&mgr, &AdminManager::apiError);

        mgr.GetUserDetail(999);

        QTRY_COMPARE(error_spy.count(), 1);
        QCOMPARE(detail_spy.count(), 0);

        auto args = error_spy.takeFirst();
        QCOMPARE(args.at(1).toInt(), 80002);
    }

    // ── ChangeUserStatus ──

    void ChangeUserStatusEmitsSuccess() {
        MockNetworkAccessManager mock_network;
        mock_network.RegisterResponse(
            "api/admin/users/2/status",
            TestJsonLoader::LoadJson("admin/admin_change_status_success.json")
        );

        NetworkClient network_client(static_cast<QNetworkAccessManager*>(&mock_network));
        network_client.SetBaseUrl("http://127.0.0.1:8080/");
        RequestFactory request_factory;
        request_factory.SetOwnerAccessToken("admin_token");
        AdminManager mgr(&network_client, &request_factory);

        QSignalSpy success_spy(&mgr, &AdminManager::operationSuccess);
        QSignalSpy error_spy(&mgr, &AdminManager::apiError);

        mgr.ChangeUserStatus(2, 0);

        QTRY_COMPARE(success_spy.count(), 1);
        QCOMPARE(error_spy.count(), 0);
    }

    void ChangeUserStatusRejectsForbiddenSelfModify() {
        MockNetworkAccessManager mock_network;
        mock_network.RegisterResponse(
            "api/admin/users/1/status",
            TestJsonLoader::LoadJson("admin/admin_change_status_forbidden.json"),
            403
        );

        NetworkClient network_client(static_cast<QNetworkAccessManager*>(&mock_network));
        network_client.SetBaseUrl("http://127.0.0.1:8080/");
        RequestFactory request_factory;
        request_factory.SetOwnerAccessToken("admin_token");
        AdminManager mgr(&network_client, &request_factory);

        QSignalSpy success_spy(&mgr, &AdminManager::operationSuccess);
        QSignalSpy error_spy(&mgr, &AdminManager::apiError);

        mgr.ChangeUserStatus(1, 0);

        QTRY_COMPARE(error_spy.count(), 1);
        QCOMPARE(success_spy.count(), 0);

        auto args = error_spy.takeFirst();
        QCOMPARE(args.at(1).toInt(), 80003);
    }

    // ── ChangeUserRole ──

    void ChangeUserRoleEmitsSuccess() {
        MockNetworkAccessManager mock_network;
        mock_network.RegisterResponse(
            "api/admin/users/2/role",
            TestJsonLoader::LoadJson("admin/admin_change_role_success.json")
        );

        NetworkClient network_client(static_cast<QNetworkAccessManager*>(&mock_network));
        network_client.SetBaseUrl("http://127.0.0.1:8080/");
        RequestFactory request_factory;
        request_factory.SetOwnerAccessToken("admin_token");
        AdminManager mgr(&network_client, &request_factory);

        QSignalSpy success_spy(&mgr, &AdminManager::operationSuccess);
        QSignalSpy error_spy(&mgr, &AdminManager::apiError);

        mgr.ChangeUserRole(2, 1);

        QTRY_COMPARE(success_spy.count(), 1);
        QCOMPARE(error_spy.count(), 0);
    }

    void ChangeUserRoleRejectsCannotDemote() {
        MockNetworkAccessManager mock_network;
        mock_network.RegisterResponse(
            "api/admin/users/1/role",
            TestJsonLoader::LoadJson("admin/admin_change_role_cannot_demote.json"),
            403
        );

        NetworkClient network_client(static_cast<QNetworkAccessManager*>(&mock_network));
        network_client.SetBaseUrl("http://127.0.0.1:8080/");
        RequestFactory request_factory;
        request_factory.SetOwnerAccessToken("admin_token");
        AdminManager mgr(&network_client, &request_factory);

        QSignalSpy success_spy(&mgr, &AdminManager::operationSuccess);
        QSignalSpy error_spy(&mgr, &AdminManager::apiError);

        mgr.ChangeUserRole(1, 0);

        QTRY_COMPARE(error_spy.count(), 1);
        QCOMPARE(success_spy.count(), 0);

        auto args = error_spy.takeFirst();
        QCOMPARE(args.at(1).toInt(), 80004);
    }

    // ── ChangeUserAvailableSpace ──

    void ChangeUserAvailableSpaceSendsRequestAndEmitsSuccess() {
        MockNetworkAccessManager mock_network;
        mock_network.RegisterResponse(
            "api/admin/users/2/available-space",
            TestJsonLoader::LoadJson("admin/admin_change_role_success.json")
        );

        NetworkClient network_client(static_cast<QNetworkAccessManager*>(&mock_network));
        network_client.SetBaseUrl("http://127.0.0.1:8080/");
        RequestFactory request_factory;
        request_factory.SetOwnerAccessToken("admin_token");
        AdminManager mgr(&network_client, &request_factory);

        QSignalSpy success_spy(&mgr, &AdminManager::operationSuccess);
        QSignalSpy error_spy(&mgr, &AdminManager::apiError);

        mgr.ChangeUserAvailableSpace(2, 50);

        QTRY_COMPARE(success_spy.count(), 1);
        QCOMPARE(error_spy.count(), 0);
        QCOMPARE(mock_network.GetRequestLog().size(), 1);
        QVERIFY(mock_network.GetRequestLog().constFirst().url().toString().contains("/api/admin/users/2/available-space"));

        auto body = QJsonDocument::fromJson(mock_network.GetRequestBodyLog().constFirst()).object();
        QCOMPARE(body.value("available_space_g").toInt(), 50);
        QCOMPARE(success_spy.takeFirst().at(0).toString(), QString("用户可用空间已更新"));
    }

    void ChangeUserAvailableSpaceHandlesApiError() {
        MockNetworkAccessManager mock_network;
        mock_network.RegisterResponse(
            "api/admin/users/999/available-space",
            QJsonObject{
                { "code", 80002 },
                { "message", "用户不存在" },
                { "data", QJsonValue::Null },
            },
            404
        );

        NetworkClient network_client(static_cast<QNetworkAccessManager*>(&mock_network));
        network_client.SetBaseUrl("http://127.0.0.1:8080/");
        RequestFactory request_factory;
        request_factory.SetOwnerAccessToken("admin_token");
        AdminManager mgr(&network_client, &request_factory);

        QSignalSpy success_spy(&mgr, &AdminManager::operationSuccess);
        QSignalSpy error_spy(&mgr, &AdminManager::apiError);

        mgr.ChangeUserAvailableSpace(999, 50);

        QTRY_COMPARE(error_spy.count(), 1);
        QCOMPARE(success_spy.count(), 0);

        auto args = error_spy.takeFirst();
        QCOMPARE(args.at(1).toInt(), 80002);
    }



    void SoftDeleteUserEmitsSuccess() {
        MockNetworkAccessManager mock_network;
        mock_network.RegisterResponse(
            "api/admin/users/5",
            TestJsonLoader::LoadJson("admin/admin_delete_user_success.json")
        );

        NetworkClient network_client(static_cast<QNetworkAccessManager*>(&mock_network));
        network_client.SetBaseUrl("http://127.0.0.1:8080/");
        RequestFactory request_factory;
        request_factory.SetOwnerAccessToken("admin_token");
        AdminManager mgr(&network_client, &request_factory);

        QSignalSpy success_spy(&mgr, &AdminManager::operationSuccess);
        QSignalSpy error_spy(&mgr, &AdminManager::apiError);

        mgr.SoftDeleteUser(5);

        QTRY_COMPARE(success_spy.count(), 1);
        QCOMPARE(error_spy.count(), 0);
    }

    // ── GetGlobalStorageStats ──

    void GetGlobalStorageStatsEmitsStorage() {
        MockNetworkAccessManager mock_network;
        mock_network.RegisterResponse(
            "api/admin/storage/stats",
            TestJsonLoader::LoadJson("admin/admin_get_global_storage_success.json")
        );

        NetworkClient network_client(static_cast<QNetworkAccessManager*>(&mock_network));
        network_client.SetBaseUrl("http://127.0.0.1:8080/");
        RequestFactory request_factory;
        request_factory.SetOwnerAccessToken("admin_token");
        AdminManager mgr(&network_client, &request_factory);

        QSignalSpy storage_spy(&mgr, &AdminManager::userStorageLoaded);
        QSignalSpy error_spy(&mgr, &AdminManager::apiError);

        mgr.GetGlobalStorageStats();

        QTRY_COMPARE(storage_spy.count(), 1);
        QCOMPARE(error_spy.count(), 0);

        auto storage = storage_spy.takeFirst().at(0).toMap();
        QCOMPARE(storage.value("total_users").toInt(), 100);
        QCOMPARE(storage.value("total_files").toInt(), 5000);
    }

    void GetGlobalStorageStatsUpdatesPropertyAndSignal() {
        MockNetworkAccessManager mock_network;
        mock_network.RegisterResponse(
            "api/admin/storage/stats",
            TestJsonLoader::LoadJson("admin/admin_get_global_storage_success.json")
        );

        NetworkClient network_client(static_cast<QNetworkAccessManager*>(&mock_network));
        network_client.SetBaseUrl("http://127.0.0.1:8080/");
        RequestFactory request_factory;
        request_factory.SetOwnerAccessToken("admin_token");
        AdminManager mgr(&network_client, &request_factory);

        QSignalSpy stats_spy(&mgr, &AdminManager::globalStorageStatsChanged);
        QSignalSpy storage_spy(&mgr, &AdminManager::userStorageLoaded);

        mgr.GetGlobalStorageStats();

        QTRY_COMPARE(stats_spy.count(), 1);
        QCOMPARE(storage_spy.count(), 1);

        auto stats = mgr.GetGlobalStorageStatsMap();
        QCOMPARE(stats.value("totalUsers").toInt(), 100);
        QCOMPARE(stats.value("totalFiles").toInt(), 5000);
        QCOMPARE(stats.value("storageUsed").toDouble(), 549755813888.0);
        QCOMPARE(stats.value("storageQuota").toDouble(), 10995116277760.0);
        QCOMPARE(stats.value("activeShares").toInt(), 150);
    }

    void ListOperationLogsPopulatesModelAndPagination() {
        MockNetworkAccessManager mock_network;
        mock_network.RegisterResponse(
            "api/logs",
            TestJsonLoader::LoadJson("admin/admin_operation_logs_success.json")
        );

        NetworkClient network_client(static_cast<QNetworkAccessManager*>(&mock_network));
        network_client.SetBaseUrl("http://127.0.0.1:8080/");
        RequestFactory request_factory;
        request_factory.SetOwnerAccessToken("admin_token");
        AdminManager mgr(&network_client, &request_factory);

        QSignalSpy pagination_spy(&mgr, &AdminManager::operationLogPaginationLoaded);
        QSignalSpy error_spy(&mgr, &AdminManager::apiError);

        mgr.ListOperationLogs(2, 20);

        QTRY_COMPARE(pagination_spy.count(), 1);
        QCOMPARE(error_spy.count(), 0);
        QCOMPARE(mgr.GetOperationLogModel()->rowCount(), 2);
        QCOMPARE(
            mgr.GetOperationLogModel()->data(mgr.GetOperationLogModel()->index(0), OperationLogListModel::ActionRole).toString(),
            QString("upload")
        );
        QCOMPARE(
            mgr.GetOperationLogModel()->data(mgr.GetOperationLogModel()->index(1), OperationLogListModel::TargetTypeRole).toString(),
            QString("folder")
        );

        auto args = pagination_spy.takeFirst();
        QCOMPARE(args.at(0).toInt(), 2);
        QCOMPARE(args.at(1).toInt(), 3);
        QCOMPARE(args.at(2).toInt(), 42);
    }

    void GetSystemInfoUpdatesProperty() {
        MockNetworkAccessManager mock_network;
        mock_network.RegisterResponse(
            "api/system/info",
            TestJsonLoader::LoadJson("admin/system_info_success.json")
        );

        NetworkClient network_client(static_cast<QNetworkAccessManager*>(&mock_network));
        network_client.SetBaseUrl("http://127.0.0.1:8080/");
        RequestFactory request_factory;
        request_factory.SetOwnerAccessToken("admin_token");
        AdminManager mgr(&network_client, &request_factory);

        QSignalSpy info_spy(&mgr, &AdminManager::systemInfoChanged);
        QSignalSpy error_spy(&mgr, &AdminManager::apiError);

        mgr.GetSystemInfo();

        QTRY_COMPARE(info_spy.count(), 1);
        QCOMPARE(error_spy.count(), 0);

        auto info = mgr.GetSystemInfoMap();
        QCOMPARE(info.value("version").toString(), QString("1.0.0"));
        QCOMPARE(info.value("drogonVersion").toString(), QString("1.9.10"));
        QCOMPARE(info.value("uptime").toInt(), 3661);
        QCOMPARE(info.value("currentConnections").toInt(), 8);
        QCOMPARE(info.value("redisPoolSize").toInt(), 4);
        QCOMPARE(info.value("totalFolders").toInt(), 300);
        QCOMPARE(info.value("totalSize").toDouble(), 549755813888.0);
    }

    // ── ListShares ──

    void ListSharesPopulatesShareModel() {
        MockNetworkAccessManager mock_network;
        mock_network.RegisterResponse(
            "api/admin/shares",
            TestJsonLoader::LoadJson("admin/admin_list_shares_success.json")
        );

        NetworkClient network_client(static_cast<QNetworkAccessManager*>(&mock_network));
        network_client.SetBaseUrl("http://127.0.0.1:8080/");
        RequestFactory request_factory;
        request_factory.SetOwnerAccessToken("admin_token");
        AdminManager mgr(&network_client, &request_factory);

        QSignalSpy pagination_spy(&mgr, &AdminManager::sharePaginationLoaded);
        QSignalSpy error_spy(&mgr, &AdminManager::apiError);

        mgr.ListShares();

        QTRY_COMPARE(pagination_spy.count(), 1);
        QCOMPARE(error_spy.count(), 0);
        QCOMPARE(mgr.GetShareModel()->rowCount(), 2);

        QCOMPARE(
            mgr.GetShareModel()->data(mgr.GetShareModel()->index(0), AdminShareListModel::FileNameRole).toString(),
            QString("project_plan.pdf")
        );
        QCOMPARE(
            mgr.GetShareModel()->data(mgr.GetShareModel()->index(1), AdminShareListModel::FileNameRole).toString(),
            QString("report.xlsx")
        );

        auto args = pagination_spy.takeFirst();
        QCOMPARE(args.at(0).toInt(), 1);
        QCOMPARE(args.at(1).toInt(), 3);
        QCOMPARE(args.at(2).toInt(), 50);
    }

    void ListSharesSendsSharerUsernameFilter() {
        MockNetworkAccessManager mock_network;
        mock_network.RegisterResponse(
            "api/admin/shares",
            TestJsonLoader::LoadJson("admin/admin_list_shares_success.json")
        );

        NetworkClient network_client(static_cast<QNetworkAccessManager*>(&mock_network));
        network_client.SetBaseUrl("http://127.0.0.1:8080/");
        RequestFactory request_factory;
        request_factory.SetOwnerAccessToken("admin_token");
        AdminManager mgr(&network_client, &request_factory);

        QSignalSpy pagination_spy(&mgr, &AdminManager::sharePaginationLoaded);
        QSignalSpy error_spy(&mgr, &AdminManager::apiError);

        mgr.ListShares(2, 50, 1, -1, QStringLiteral("alice"));

        QTRY_COMPARE(pagination_spy.count(), 1);
        QCOMPARE(error_spy.count(), 0);
        QCOMPARE(mock_network.GetRequestLog().size(), 1);

        auto query = QUrlQuery(mock_network.GetRequestLog().first().url());
        QCOMPARE(query.queryItemValue("page"), QString("2"));
        QCOMPARE(query.queryItemValue("page_size"), QString("50"));
        QCOMPARE(query.queryItemValue("status"), QString("1"));
        QCOMPARE(query.queryItemValue("username"), QString("alice"));
        QVERIFY(!query.hasQueryItem("user_id"));
    }

    // ── GetShareDetail ──

    void GetShareDetailEmitsDetail() {
        MockNetworkAccessManager mock_network;
        mock_network.RegisterResponse(
            "api/admin/shares/1",
            TestJsonLoader::LoadJson("admin/admin_get_share_detail_success.json")
        );

        NetworkClient network_client(static_cast<QNetworkAccessManager*>(&mock_network));
        network_client.SetBaseUrl("http://127.0.0.1:8080/");
        RequestFactory request_factory;
        request_factory.SetOwnerAccessToken("admin_token");
        AdminManager mgr(&network_client, &request_factory);

        QSignalSpy detail_spy(&mgr, &AdminManager::shareDetailLoaded);
        QSignalSpy error_spy(&mgr, &AdminManager::apiError);

        mgr.GetShareDetail(1);

        QTRY_COMPARE(detail_spy.count(), 1);
        QCOMPARE(error_spy.count(), 0);

        auto detail = detail_spy.takeFirst().at(0).toMap();
        QCOMPARE(detail.value("file_name").toString(), QString("report.pdf"));
        QCOMPARE(detail.value("username").toString(), QString("alice"));
        QCOMPARE(detail.value("status").toInt(), 1);
        QCOMPARE(detail.value("password_set").toBool(), true);
    }

    // ── ForceCancelShare ──

    void ForceCancelShareEmitsSuccess() {
        MockNetworkAccessManager mock_network;
        mock_network.RegisterResponse(
            "api/admin/shares/10",
            TestJsonLoader::LoadJson("admin/admin_force_cancel_share_success.json")
        );

        NetworkClient network_client(static_cast<QNetworkAccessManager*>(&mock_network));
        network_client.SetBaseUrl("http://127.0.0.1:8080/");
        RequestFactory request_factory;
        request_factory.SetOwnerAccessToken("admin_token");
        AdminManager mgr(&network_client, &request_factory);

        QSignalSpy success_spy(&mgr, &AdminManager::operationSuccess);
        QSignalSpy error_spy(&mgr, &AdminManager::apiError);

        mgr.ForceCancelShare(10);

        QTRY_COMPARE(success_spy.count(), 1);
        QCOMPARE(error_spy.count(), 0);
    }

    void ForceCancelShareRejectsNotFound() {
        MockNetworkAccessManager mock_network;
        mock_network.RegisterResponse(
            "api/admin/shares/9999",
            TestJsonLoader::LoadJson("admin/admin_force_cancel_share_not_found.json"),
            404
        );

        NetworkClient network_client(static_cast<QNetworkAccessManager*>(&mock_network));
        network_client.SetBaseUrl("http://127.0.0.1:8080/");
        RequestFactory request_factory;
        request_factory.SetOwnerAccessToken("admin_token");
        AdminManager mgr(&network_client, &request_factory);

        QSignalSpy success_spy(&mgr, &AdminManager::operationSuccess);
        QSignalSpy error_spy(&mgr, &AdminManager::apiError);

        mgr.ForceCancelShare(9999);

        QTRY_COMPARE(error_spy.count(), 1);
        QCOMPARE(success_spy.count(), 0);

        auto args = error_spy.takeFirst();
        QCOMPARE(args.at(1).toInt(), 80005);
    }

    // ── GetOverviewStatsApi ──

    void GetOverviewStatsApiUpdatesProperty() {
        MockNetworkAccessManager mock_network;
        mock_network.RegisterResponse(
            "api/admin/stats/overview",
            TestJsonLoader::LoadJson("admin/admin_stats_overview_success.json")
        );

        NetworkClient network_client(static_cast<QNetworkAccessManager*>(&mock_network));
        network_client.SetBaseUrl("http://127.0.0.1:8080/");
        RequestFactory request_factory;
        request_factory.SetOwnerAccessToken("admin_token");
        AdminManager mgr(&network_client, &request_factory);

        QSignalSpy stats_spy(&mgr, &AdminManager::overviewStatsChanged);
        QSignalSpy error_spy(&mgr, &AdminManager::apiError);

        mgr.GetOverviewStatsApi();

        QTRY_COMPARE(stats_spy.count(), 1);
        QCOMPARE(error_spy.count(), 0);

        auto stats = mgr.GetOverviewStats();
        QCOMPARE(stats.value("totalUsers").toInt(), 100);
        QCOMPARE(stats.value("totalFiles").toInt(), 5000);
        QCOMPARE(stats.value("activeShares").toInt(), 150);
    }

    // ── GetSystemStatusApi ──

    void GetSystemStatusApiUpdatesProperty() {
        MockNetworkAccessManager mock_network;
        mock_network.RegisterResponse(
            "api/admin/stats/system",
            TestJsonLoader::LoadJson("admin/admin_stats_system_success.json")
        );

        NetworkClient network_client(static_cast<QNetworkAccessManager*>(&mock_network));
        network_client.SetBaseUrl("http://127.0.0.1:8080/");
        RequestFactory request_factory;
        request_factory.SetOwnerAccessToken("admin_token");
        AdminManager mgr(&network_client, &request_factory);

        QSignalSpy status_spy(&mgr, &AdminManager::systemStatusChanged);
        QSignalSpy error_spy(&mgr, &AdminManager::apiError);

        mgr.GetSystemStatusApi();

        QTRY_COMPARE(status_spy.count(), 1);
        QCOMPARE(error_spy.count(), 0);

        auto status = mgr.GetSystemStatus();
        QCOMPARE(status.value("dbConnected").toBool(), true);
        QCOMPARE(status.value("redisConnected").toBool(), true);
        QCOMPARE(status.value("uptime").toString(), QString("1d 0h 0m"));
    }

    void GetSystemStatusApiHandlesError() {
        MockNetworkAccessManager mock_network;
        mock_network.RegisterResponse(
            "api/admin/stats/system",
            TestJsonLoader::LoadJson("admin/admin_not_authorized.json"),
            403
        );

        NetworkClient network_client(static_cast<QNetworkAccessManager*>(&mock_network));
        network_client.SetBaseUrl("http://127.0.0.1:8080/");
        RequestFactory request_factory;
        request_factory.SetOwnerAccessToken("user_token");
        AdminManager mgr(&network_client, &request_factory);

        QSignalSpy status_spy(&mgr, &AdminManager::systemStatusChanged);
        QSignalSpy error_spy(&mgr, &AdminManager::apiError);

        mgr.GetSystemStatusApi();

        QTRY_COMPARE(error_spy.count(), 1);
        QCOMPARE(status_spy.count(), 0);
    }

    // ── CamelCase key verification ──

    void GetOverviewStatsApiCamelCaseKeys() {
        MockNetworkAccessManager mock_network;
        mock_network.RegisterResponse(
            "api/admin/stats/overview",
            TestJsonLoader::LoadJson("admin/admin_stats_overview_success.json")
        );

        NetworkClient network_client(static_cast<QNetworkAccessManager*>(&mock_network));
        network_client.SetBaseUrl("http://127.0.0.1:8080/");
        RequestFactory request_factory;
        request_factory.SetOwnerAccessToken("admin_token");
        AdminManager mgr(&network_client, &request_factory);

        QSignalSpy stats_spy(&mgr, &AdminManager::overviewStatsChanged);

        mgr.GetOverviewStatsApi();

        QTRY_COMPARE(stats_spy.count(), 1);

        auto stats = mgr.GetOverviewStats();
        QCOMPARE(stats.value("totalUsers").toInt(), 100);
        QCOMPARE(stats.value("totalFiles").toInt(), 5000);
        QCOMPARE(stats.value("storageUsed").toDouble(), 549755813888.0);
        QCOMPARE(stats.value("storageQuota").toDouble(), 10995116277760.0);
        QCOMPARE(stats.value("activeShares").toInt(), 150);

        // Verify old snake_case keys are absent
        QVERIFY(!stats.contains("total_users"));
        QVERIFY(!stats.contains("total_files"));
        QVERIFY(!stats.contains("total_storage_used"));
        QVERIFY(!stats.contains("total_storage_quota"));
        QVERIFY(!stats.contains("active_shares"));
    }

    void GetSystemStatusApiCamelCaseKeys() {
        MockNetworkAccessManager mock_network;
        mock_network.RegisterResponse(
            "api/admin/stats/system",
            TestJsonLoader::LoadJson("admin/admin_stats_system_success.json")
        );

        NetworkClient network_client(static_cast<QNetworkAccessManager*>(&mock_network));
        network_client.SetBaseUrl("http://127.0.0.1:8080/");
        RequestFactory request_factory;
        request_factory.SetOwnerAccessToken("admin_token");
        AdminManager mgr(&network_client, &request_factory);

        QSignalSpy status_spy(&mgr, &AdminManager::systemStatusChanged);

        mgr.GetSystemStatusApi();

        QTRY_COMPARE(status_spy.count(), 1);

        auto status = mgr.GetSystemStatus();
        QCOMPARE(status.value("dbConnected").toBool(), true);
        QCOMPARE(status.value("redisConnected").toBool(), true);
        // disk_used=5497558138880, disk_total=10995116277760 → 50.0%
        QCOMPARE(status.value("diskUsage").toDouble(), 50.0);
        QCOMPARE(status.value("uptime").toString(), QString("1d 0h 0m"));

        // Verify raw snake_case API keys are absent from QML-facing model
        QVERIFY(!status.contains("db_connected"));
        QVERIFY(!status.contains("redis_connected"));
        QVERIFY(!status.contains("disk_total"));
        QVERIFY(!status.contains("disk_used"));
        QVERIFY(!status.contains("uptime_seconds"));
    }

    void GetSystemStatusApiDiskUsageZeroDivision() {
        MockNetworkAccessManager mock_network;
        QJsonObject zero_disk_response;
        zero_disk_response["code"] = 0;
        zero_disk_response["message"] = "success";
        QJsonObject data;
        data["db_connected"] = true;
        data["redis_connected"] = true;
        data["disk_total"] = 0;
        data["disk_used"] = 0;
        data["disk_free"] = 0;
        data["uptime_seconds"] = 45;
        zero_disk_response["data"] = data;
        mock_network.RegisterResponse("api/admin/stats/system", zero_disk_response);

        NetworkClient network_client(static_cast<QNetworkAccessManager*>(&mock_network));
        network_client.SetBaseUrl("http://127.0.0.1:8080/");
        RequestFactory request_factory;
        request_factory.SetOwnerAccessToken("admin_token");
        AdminManager mgr(&network_client, &request_factory);

        QSignalSpy status_spy(&mgr, &AdminManager::systemStatusChanged);

        mgr.GetSystemStatusApi();

        QTRY_COMPARE(status_spy.count(), 1);

        auto status = mgr.GetSystemStatus();
        // Zero-division guard: diskUsage must be 0.0, not NaN/inf
        QCOMPARE(status.value("diskUsage").toDouble(), 0.0);
        // 45 seconds → "45s" (less than a minute)
        QCOMPARE(status.value("uptime").toString(), QString("45s"));
    }

    // ── Auth header verification ──

    void ListUsersSendsOwnerAuthHeaders() {
        MockNetworkAccessManager mock_network;
        mock_network.RegisterResponse(
            "api/admin/users",
            TestJsonLoader::LoadJson("admin/admin_list_users_empty.json")
        );

        NetworkClient network_client(static_cast<QNetworkAccessManager*>(&mock_network));
        network_client.SetBaseUrl("http://127.0.0.1:8080/");
        RequestFactory request_factory;
        request_factory.SetOwnerAccessToken("admin_access_token");
        AdminManager mgr(&network_client, &request_factory);

        QSignalSpy pagination_spy(&mgr, &AdminManager::userPaginationLoaded);

        mgr.ListUsers();

        QTRY_COMPARE(pagination_spy.count(), 1);

        auto request = mock_network.GetRequestLog().constFirst();
        QCOMPARE(
            request.rawHeader("Authorization"),
            QByteArray("Bearer admin_access_token")
        );
    }

    // ── Malformed response handling ──

    void ChangeUserStatusRejectsMalformedResponse() {
        MockNetworkAccessManager mock_network;
        mock_network.RegisterRawResponse("api/admin/users/2/status", "not-json", 200);

        NetworkClient network_client(static_cast<QNetworkAccessManager*>(&mock_network));
        network_client.SetBaseUrl("http://127.0.0.1:8080/");
        RequestFactory request_factory;
        request_factory.SetOwnerAccessToken("admin_token");
        AdminManager mgr(&network_client, &request_factory);

        QSignalSpy success_spy(&mgr, &AdminManager::operationSuccess);
        QSignalSpy error_spy(&mgr, &AdminManager::apiError);

        mgr.ChangeUserStatus(2, 0);

        QTRY_COMPARE(error_spy.count(), 1);
        QCOMPARE(success_spy.count(), 0);
    }

    void GetOverviewStatsApiRejectsMalformedResponse() {
        MockNetworkAccessManager mock_network;
        mock_network.RegisterRawResponse("api/admin/stats/overview", "not-json", 200);

        NetworkClient network_client(static_cast<QNetworkAccessManager*>(&mock_network));
        network_client.SetBaseUrl("http://127.0.0.1:8080/");
        RequestFactory request_factory;
        request_factory.SetOwnerAccessToken("admin_token");
        AdminManager mgr(&network_client, &request_factory);

        QSignalSpy stats_spy(&mgr, &AdminManager::overviewStatsChanged);
        QSignalSpy error_spy(&mgr, &AdminManager::apiError);

        mgr.GetOverviewStatsApi();

        QTRY_COMPARE(error_spy.count(), 1);
        QCOMPARE(stats_spy.count(), 0);
    }
};

int run_TestAdminManager(int argc, char* argv[]) {
    TestAdminManager test;
    return QTest::qExec(&test, argc, argv);
}

#include "test_admin_manager.moc"
