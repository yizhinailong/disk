#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFile>
#include <QSignalSpy>
#include <QString>
#include <QTest>

#include "auth/AuthService.hpp"
#include "helpers/MockNetworkAccessManager.hpp"
#include "helpers/MockReplyFactory.hpp"
#include "managers/DriveManager.hpp"
#include "network/NetworkClient.hpp"
#include "network/RequestFactory.hpp"

using namespace disk::desktop;
using namespace disk::desktop::testing;

class DesktopStubTest : public QObject {
    Q_OBJECT

private slots:

    void initTestCase() {
        QVERIFY(true);
    }

    void stubTest() {
        QCOMPARE(1 + 1, 2);
    }

    void loginRequestUsesBackendAccountField() {
        MockNetworkAccessManager mock_network;
        mock_network.RegisterResponse(
            "api/auth/login",
            MockReplyFactory::LoginSuccessResponse()
        );

        NetworkClient network_client(static_cast<QNetworkAccessManager*>(&mock_network));
        RequestFactory request_factory;
        AuthService auth_service(&network_client, &request_factory);

        QSignalSpy success_spy(&auth_service, &AuthService::loginSuccess);
        QSignalSpy failure_spy(&auth_service, &AuthService::loginFailure);

        auth_service.Login("testuser", "Password1");

        QTRY_COMPARE(success_spy.count(), 1);
        QCOMPARE(failure_spy.count(), 0);
        QCOMPARE(
            mock_network.GetRequestLog().constFirst().url().toString(),
            QString("http://127.0.0.1:8080/api/auth/login")
        );
        QCOMPARE(mock_network.GetRequestBodyLog().size(), 1);

        auto body = QJsonDocument::fromJson(mock_network.GetRequestBodyLog().constFirst()).object();
        QCOMPARE(body.value("account").toString(), QString("testuser"));
        QCOMPARE(body.value("password").toString(), QString("Password1"));
        QVERIFY(!body.contains("username"));
    }

    void registerSuccessEmitsRegisterSuccess() {
        MockNetworkAccessManager mock_network;
        mock_network.RegisterResponse(
            "api/auth/register",
            MockReplyFactory::RegisterSuccessResponse()
        );

        NetworkClient network_client(static_cast<QNetworkAccessManager*>(&mock_network));
        network_client.SetBaseUrl("http://127.0.0.1:8080/");
        RequestFactory request_factory;
        AuthService auth_service(&network_client, &request_factory);

        QSignalSpy success_spy(&auth_service, &AuthService::registerSuccess);
        QSignalSpy failure_spy(&auth_service, &AuthService::registerFailure);

        auth_service.Register("newuser", "newuser@example.com", "Password1");

        QTRY_COMPARE(success_spy.count(), 1);
        QCOMPARE(failure_spy.count(), 0);
        QCOMPARE(mock_network.GetRequestLog().size(), 1);

        auto arguments = success_spy.takeFirst();
        auto user = arguments.at(0).toJsonObject();
        QCOMPARE(user.value("username").toString(), QString("newuser"));
        QCOMPARE(user.value("email").toString(), QString("newuser@example.com"));
    }

    void registerFailureEmitsRegisterFailure() {
        MockNetworkAccessManager mock_network;
        mock_network.RegisterResponse(
            "api/auth/register",
            QJsonObject{
                { "code", 40001 },
                { "message", "用户名已存在" },
                { "data", QJsonValue::Null },
            },
            409
        );

        NetworkClient network_client(static_cast<QNetworkAccessManager*>(&mock_network));
        network_client.SetBaseUrl("http://127.0.0.1:8080/");
        RequestFactory request_factory;
        AuthService auth_service(&network_client, &request_factory);

        QSignalSpy success_spy(&auth_service, &AuthService::registerSuccess);
        QSignalSpy failure_spy(&auth_service, &AuthService::registerFailure);

        auth_service.Register("newuser", "newuser@example.com", "Password1");

        QTRY_COMPARE(failure_spy.count(), 1);
        QCOMPARE(success_spy.count(), 0);

        auto arguments = failure_spy.takeFirst();
        QCOMPARE(arguments.at(0).toInt(), 40001);
        QCOMPARE(arguments.at(1).toString(), QString("用户名已存在"));
    }

    void driveListFilesEmitsPaginationForEmptyFolder() {
        MockNetworkAccessManager mock_network;
        mock_network.RegisterResponse(
            "api/file/list",
            QJsonObject{
                { "code", 0 },
                { "message", "success" },
                { "data",
                 QJsonObject{
                     { "items", QJsonArray{} },
                     { "pagination",
                      QJsonObject{
                          { "page", 1 },
                          { "page_size", 50 },
                          { "total", 0 },
                          { "total_pages", 0 },
                      } },
                 } },
            }
        );

        NetworkClient network_client(static_cast<QNetworkAccessManager*>(&mock_network));
        RequestFactory request_factory;
        request_factory.SetOwnerAccessToken("access-token");
        managers::DriveManager drive_manager(&network_client, &request_factory);

        QSignalSpy pagination_spy(&drive_manager, &managers::DriveManager::paginationLoaded);
        QSignalSpy error_spy(&drive_manager, &managers::DriveManager::apiError);

        drive_manager.listFiles("0");

        QTRY_COMPARE(pagination_spy.count(), 1);
        QCOMPARE(error_spy.count(), 0);

        auto arguments = pagination_spy.takeFirst();
        QCOMPARE(arguments.at(0).toInt(), 1);
        QCOMPARE(arguments.at(1).toInt(), 0);
        QCOMPARE(arguments.at(2).toInt(), 0);
        QCOMPARE(
            mock_network.GetRequestLog().constFirst().url().toString(),
            QString(
                "http://127.0.0.1:8080/api/file/list?parent_id=0&page=1&page_size=50&sort_by=name&sort_order=asc"
            )
        );
    }

    void driveListFilesParsesTopLevelErrorEnvelope() {
        MockNetworkAccessManager mock_network;
        mock_network.RegisterResponse(
            "api/file/list",
            QJsonObject{
                { "code", 40106 },
                { "message", "Token missing" },
                { "data", QJsonValue::Null },
            },
            401
        );

        NetworkClient network_client(static_cast<QNetworkAccessManager*>(&mock_network));
        RequestFactory request_factory;
        managers::DriveManager drive_manager(&network_client, &request_factory);

        QSignalSpy pagination_spy(&drive_manager, &managers::DriveManager::paginationLoaded);
        QSignalSpy error_spy(&drive_manager, &managers::DriveManager::apiError);
        QSignalSpy list_error_spy(&drive_manager, &managers::DriveManager::listLoadFailed);

        drive_manager.listFiles("0");

        QTRY_COMPARE(error_spy.count(), 1);
        QCOMPARE(pagination_spy.count(), 0);
        QCOMPARE(list_error_spy.count(), 1);

        auto arguments = error_spy.takeFirst();
        QCOMPARE(arguments.at(0).toString(), QString("Token missing"));
        QCOMPARE(arguments.at(1).toInt(), 40106);

        auto list_error_arguments = list_error_spy.takeFirst();
        QCOMPARE(list_error_arguments.at(0).toString(), QString("Token missing"));
        QCOMPARE(list_error_arguments.at(1).toInt(), 40106);
    }

    void driveRootBreadcrumbIsLocal() {
        MockNetworkAccessManager mock_network;
        NetworkClient network_client(static_cast<QNetworkAccessManager*>(&mock_network));
        RequestFactory request_factory;
        managers::DriveManager drive_manager(&network_client, &request_factory);

        QSignalSpy breadcrumb_spy(&drive_manager, &managers::DriveManager::breadcrumbLoaded);

        drive_manager.loadBreadcrumb("0");

        QCOMPARE(breadcrumb_spy.count(), 1);
        QCOMPARE(mock_network.GetRequestLog().size(), 0);

        auto breadcrumb = breadcrumb_spy.takeFirst().at(0).toList();
        QCOMPARE(breadcrumb.size(), 1);
        auto root = breadcrumb.constFirst().toMap();
        QCOMPARE(root.value("id").toDouble(), 0.0);
        QCOMPARE(root.value("name").toString(), QString("根目录"));
    }

    void driveBreadcrumbParsesBackendPath() {
        MockNetworkAccessManager mock_network;
        mock_network.RegisterResponse(
            "api/folder/42/breadcrumb",
            QJsonObject{
                { "code", 0 },
                { "message", "success" },
                { "data",
                 QJsonObject{
                     { "path",
                      QJsonArray{
                          QJsonObject{ { "id", 0 }, { "name", "根目录" } },
                          QJsonObject{ { "id", 42 }, { "name", "docs" } },
                      } },
                 } },
            }
        );

        NetworkClient network_client(static_cast<QNetworkAccessManager*>(&mock_network));
        RequestFactory request_factory;
        request_factory.SetOwnerAccessToken("access-token");
        managers::DriveManager drive_manager(&network_client, &request_factory);

        QSignalSpy breadcrumb_spy(&drive_manager, &managers::DriveManager::breadcrumbLoaded);
        QSignalSpy error_spy(&drive_manager, &managers::DriveManager::apiError);

        drive_manager.loadBreadcrumb("42");

        QTRY_COMPARE(breadcrumb_spy.count(), 1);
        QCOMPARE(error_spy.count(), 0);

        auto breadcrumb = breadcrumb_spy.takeFirst().at(0).toList();
        QCOMPARE(breadcrumb.size(), 2);
        QCOMPARE(breadcrumb.at(1).toMap().value("id").toDouble(), 42.0);
        QCOMPARE(breadcrumb.at(1).toMap().value("name").toString(), QString("docs"));
    }

    void ownerShellSidebarUsesComponentNavigation() {
        QFile owner_shell(QStringLiteral(DESKTOP_QML_SOURCE_DIR "/shells/OwnerShell.qml"));
        QVERIFY(owner_shell.open(QIODevice::ReadOnly));

        const auto qml = QString::fromUtf8(owner_shell.readAll());
        QVERIFY(!qml.contains(QStringLiteral("stackView.replace(\"DriveBrowserPage.qml\")")));
        QVERIFY(!qml.contains(QStringLiteral("stackView.replace(\"TransferCenterPage.qml\")")));
        QVERIFY(!qml.contains(QStringLiteral("stackView.replace(\"ShareManagementPage.qml\")")));
        QVERIFY(!qml.contains(QStringLiteral("stackView.replace(\"TrashPage.qml\")")));
        QVERIFY(qml.contains(QStringLiteral("root.showPage(driveBrowserPageComponent)")));
        QVERIFY(qml.contains(QStringLiteral("root.showPage(transferCenterPageComponent)")));
        QVERIFY(qml.contains(QStringLiteral("root.showPage(shareManagementPageComponent)")));
        QVERIFY(qml.contains(QStringLiteral("root.showPage(trashPageComponent)")));
        QVERIFY(qml.contains(QStringLiteral("root.showPage(settingsPageComponent)")));
    }

    void visitorShellUsesComponentNavigation() {
        QFile visitor_shell(QStringLiteral(DESKTOP_QML_SOURCE_DIR "/shells/VisitorShell.qml"));
        QVERIFY(visitor_shell.open(QIODevice::ReadOnly));

        const auto qml = QString::fromUtf8(visitor_shell.readAll());
        QVERIFY(!qml.contains(QStringLiteral("\"ShareBrowsePage.qml\"")));
        QVERIFY(qml.contains(QStringLiteral("stackView.replace(null, shareBrowsePageComponent")));
        QVERIFY(qml.contains(QStringLiteral("id: shareBrowsePageComponent")));
    }

    void cleanupTestCase() {
        QVERIFY(true);
    }
};

QTEST_MAIN(DesktopStubTest)
#include "main.moc"
