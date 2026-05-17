#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTest>

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
        QVERIFY(mgr.GetOverviewStats().isEmpty());
        QVERIFY(mgr.GetSystemStatus().isEmpty());
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
        QCOMPARE(detail.value("username").toString(), QString("alice"));
        QCOMPARE(detail.value("email").toString(), QString("alice@example.com"));
        QCOMPARE(detail.value("role").toInt(), 0);
        QCOMPARE(detail.value("status").toInt(), 1);
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

    // ── SoftDeleteUser ──

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

    // ── ListUserFiles ──

    void ListUserFilesEmitsPagination() {
        MockNetworkAccessManager mock_network;
        mock_network.RegisterResponse(
            "api/admin/users/1/files",
            TestJsonLoader::LoadJson("admin/admin_list_user_files_success.json")
        );

        NetworkClient network_client(static_cast<QNetworkAccessManager*>(&mock_network));
        network_client.SetBaseUrl("http://127.0.0.1:8080/");
        RequestFactory request_factory;
        request_factory.SetOwnerAccessToken("admin_token");
        AdminManager mgr(&network_client, &request_factory);

        QSignalSpy pagination_spy(&mgr, &AdminManager::userFilesPaginationLoaded);
        QSignalSpy error_spy(&mgr, &AdminManager::apiError);

        mgr.ListUserFiles(1);

        QTRY_COMPARE(pagination_spy.count(), 1);
        QCOMPARE(error_spy.count(), 0);

        auto args = pagination_spy.takeFirst();
        QCOMPARE(args.at(0).toInt(), 1);
        QCOMPARE(args.at(1).toInt(), 3);
        QCOMPARE(args.at(2).toInt(), 50);

        auto files = args.at(3).toList();
        QCOMPARE(files.size(), 1);
        auto first_file = files.at(0).toMap();
        QCOMPARE(first_file.value("name").toString(), QString("report.pdf"));
        QCOMPARE(first_file.value("type").toString(), QString("file"));
        QVERIFY(first_file.contains("mime_type"));
    }

    // ── GetUserStorage ──

    void GetUserStorageEmitsStorage() {
        MockNetworkAccessManager mock_network;
        mock_network.RegisterResponse(
            "api/admin/users/1/storage",
            TestJsonLoader::LoadJson("admin/admin_get_user_storage_success.json")
        );

        NetworkClient network_client(static_cast<QNetworkAccessManager*>(&mock_network));
        network_client.SetBaseUrl("http://127.0.0.1:8080/");
        RequestFactory request_factory;
        request_factory.SetOwnerAccessToken("admin_token");
        AdminManager mgr(&network_client, &request_factory);

        QSignalSpy storage_spy(&mgr, &AdminManager::userStorageLoaded);
        QSignalSpy error_spy(&mgr, &AdminManager::apiError);

        mgr.GetUserStorage(1);

        QTRY_COMPARE(storage_spy.count(), 1);
        QCOMPARE(error_spy.count(), 0);

        auto storage = storage_spy.takeFirst().at(0).toMap();
        QCOMPARE(storage.value("total_files").toInt(), 150);
        QCOMPARE(storage.value("active_shares").toInt(), 5);
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
        QCOMPARE(stats.value("total_users").toInt(), 100);
        QCOMPARE(stats.value("total_files").toInt(), 5000);
        QCOMPARE(stats.value("active_shares").toInt(), 150);
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
        QCOMPARE(status.value("mysql_connected").toBool(), true);
        QCOMPARE(status.value("redis_connected").toBool(), true);
        QCOMPARE(status.value("uptime_seconds").toInt(), 86400);
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
