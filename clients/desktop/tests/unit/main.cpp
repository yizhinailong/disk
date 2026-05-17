#include <cstdio>

#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QString>
#include <QStringList>
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
                { "message", QString::fromUtf8("用户名已存在") },
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
        QCOMPARE(arguments.at(1).toString(), QString::fromUtf8("用户名已存在"));
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
        QCOMPARE(root.value("name").toString(), QString::fromUtf8("根目录"));
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
                          QJsonObject{ { "id", 0 }, { "name", QString::fromUtf8("根目录") } },
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

        // DOC-01 §4, DOC-03 §2.2: three top-level pages only
        QVERIFY(qml.contains(QStringLiteral("id: driveBrowserPageComponent")));
        QVERIFY(qml.contains(QStringLiteral("id: transferCenterPageComponent")));
        QVERIFY(qml.contains(QStringLiteral("id: settingsPageComponent")));

        QVERIFY(qml.contains(QStringLiteral("root.showPage(transferCenterPageComponent)")));
        QVERIFY(qml.contains(QStringLiteral("root.showPage(settingsPageComponent)")));
        QVERIFY(qml.contains(QStringLiteral("root.showDriveViewMode(\"myfiles\")")));
        QVERIFY(qml.contains(QStringLiteral("root.showDriveViewMode(\"shared\")")));
QVERIFY(qml.contains(QStringLiteral("root.showDriveViewMode(\"trash\")")));
        QVERIFY(qml.contains(QStringLiteral("id: \"myfiles\"")));
        QVERIFY(qml.contains(QStringLiteral("id: \"shared\"")));
QVERIFY(qml.contains(QStringLiteral("id: \"trash\"")));
        QVERIFY(qml.contains(QStringLiteral("enabled: false")));

        QVERIFY(qml.contains(QStringLiteral("property string activeDestination: \"drive\"")));
        QVERIFY(qml.contains(QStringLiteral("property string activeDriveViewMode: \"myfiles\"")));
        QVERIFY(qml.contains(QStringLiteral("root.activeDestination = \"drive\"")));
        QVERIFY(qml.contains(QStringLiteral("root.activeDestination = \"transfers\"")));
        QVERIFY(qml.contains(QStringLiteral("root.activeDestination = \"settings\"")));
        QVERIFY(!qml.contains(QStringLiteral("root.activeDestination = \"shares\"")));
        QVERIFY(!qml.contains(QStringLiteral("root.activeDestination = \"trash\"")));
        QVERIFY(!qml.contains(QStringLiteral("root.activeDestination = \"files\"")));

        QVERIFY(qml.contains(QStringLiteral("title: \"File Views\"")));
        QVERIFY(qml.contains(QStringLiteral("title: \"Independent Pages\"")));
QVERIFY(qml.contains(QStringLiteral("label: \"My Files\"")));
        QVERIFY(qml.contains(QStringLiteral("label: \"Transfers\"")));
        QVERIFY(qml.contains(QStringLiteral("text: \"Logout\"")));
QVERIFY(qml.contains(QStringLiteral("label: \"Shares\"")));
        QVERIFY(qml.contains(QStringLiteral("label: \"Settings\"")));
        QVERIFY(qml.contains(QStringLiteral("text: \"Navigation\"")));
        QVERIFY(qml.contains(QStringLiteral("text: \"Session\"")));
        QVERIFY(qml.contains(QStringLiteral("function showPage(pageComponent)")));
        QVERIFY(qml.contains(QStringLiteral("function showDriveViewMode(viewMode)")));

        QVERIFY(!qml.contains(QStringLiteral("PageStateView")));
        QVERIFY(!qml.contains(QStringLiteral("BreadcrumbBar")));
        QVERIFY(!qml.contains(QStringLiteral("FolderTreePanel")));

        const auto read_qml = [](const QString& relative_path) -> QString {
            QFile qml_file(QStringLiteral(DESKTOP_QML_SOURCE_DIR "/") + relative_path);
            if (!qml_file.open(QIODevice::ReadOnly)) {
                return {};
            }
            return QString::fromUtf8(qml_file.readAll());
        };

        const auto drive_browser_page_qml = read_qml(QStringLiteral("pages/DriveBrowserPage.qml"));
        const auto drive_toolbar_qml = read_qml(QStringLiteral("components/drive/DriveToolbarCard.qml"));
        const auto drive_status_qml = read_qml(QStringLiteral("components/drive/DriveStatusCard.qml"));
        const auto drive_my_files_qml = read_qml(QStringLiteral("components/drive/DriveMyFilesView.qml"));
        const auto drive_shared_qml = read_qml(QStringLiteral("components/drive/DriveSharedView.qml"));
const auto drive_trash_qml = read_qml(QStringLiteral("components/drive/DriveTrashView.qml"));

        QVERIFY(!drive_browser_page_qml.isEmpty());
        QVERIFY(!drive_toolbar_qml.isEmpty());
        QVERIFY(!drive_status_qml.isEmpty());
        QVERIFY(!drive_my_files_qml.isEmpty());
        QVERIFY(!drive_shared_qml.isEmpty());
        QVERIFY(!drive_trash_qml.isEmpty());

        QStringList drive_sources;
        drive_sources << drive_browser_page_qml
                      << drive_toolbar_qml
                      << drive_status_qml
                      << drive_my_files_qml
                      << drive_shared_qml
                      << drive_trash_qml;

        const auto drive_qml = drive_sources.join(QStringLiteral("\n"));

        QVERIFY(drive_qml.contains(QStringLiteral("PageStateView")));
        QVERIFY(drive_qml.contains(QStringLiteral("BreadcrumbBar")));
        QVERIFY(drive_qml.contains(QStringLiteral("FolderTreePanel")));
        QVERIFY(drive_qml.contains(QStringLiteral("property string currentViewMode: \"myfiles\"")));
        QVERIFY(drive_qml.contains(QStringLiteral("function activateViewMode(mode)")));
    }

    void visitorShellUsesComponentNavigation() {
        QFile visitor_shell(QStringLiteral(DESKTOP_QML_SOURCE_DIR "/shells/VisitorShell.qml"));
        QVERIFY(visitor_shell.open(QIODevice::ReadOnly));

        const auto qml = QString::fromUtf8(visitor_shell.readAll());
        QVERIFY(!qml.contains(QStringLiteral("\"ShareBrowsePage.qml\"")));
        QVERIFY(qml.contains(QStringLiteral("stackView.replace(null, shareBrowsePageComponent")));
        QVERIFY(qml.contains(QStringLiteral("id: shareBrowsePageComponent")));
        QVERIFY(qml.contains(QStringLiteral("onPageStateChanged")));
        QVERIFY(!qml.contains(QStringLiteral("onCurrentShellChanged")));
        QVERIFY(qml.contains(QStringLiteral("sessionStore.visitor.shareId")));
    }

    void shareVerifyPageDrivesVisitorStateMachine() {
        QFile verify_page(QStringLiteral(DESKTOP_QML_SOURCE_DIR "/pages/ShareVerifyPage.qml"));
        QVERIFY(verify_page.open(QIODevice::ReadOnly));

        const auto qml = QString::fromUtf8(verify_page.readAll());
        QVERIFY(qml.contains(QStringLiteral("sessionStore.visitor.StartVerify")));
        QVERIFY(!qml.contains(QStringLiteral("authService.AccessShare")));
        QVERIFY(qml.contains(QStringLiteral("sessionStore.ActivateVisitor")));
        QVERIFY(qml.contains(QStringLiteral("onShareAccessFailure")));
        QVERIFY(!qml.contains(QStringLiteral("onShareAccessSuccess")));
        QVERIFY(!qml.contains(QStringLiteral("navigateToVisitor")));
    }

    void cleanupTestCase() {
        QVERIFY(true);
    }
};

// ── External test classes (each in their own .cpp with #include "*.moc") ──

extern int run_TestAuthServiceLogout(int argc, char* argv[]);
extern int run_TestAuthServiceRefresh(int argc, char* argv[]);
extern int run_TestSessionStore(int argc, char* argv[]);
extern int run_TestOwnerSession(int argc, char* argv[]);
extern int run_TestVisitorSession(int argc, char* argv[]);
extern int run_TestVisitorFlow(int argc, char* argv[]);
extern int run_TestDriveItemMapping(int argc, char* argv[]);
extern int run_TestDriveListModel(int argc, char* argv[]);
extern int run_TestAdminShareListModel(int argc, char* argv[]);
extern int run_TestErrorAdapter(int argc, char* argv[]);
extern int run_TestFolderTreeModel(int argc, char* argv[]);
extern int run_TestUploadTaskModel(int argc, char* argv[]);
extern int run_TestDownloadTaskModel(int argc, char* argv[]);
extern int run_TestAdminUserListModel(int argc, char* argv[]);
extern int run_TestShellController(int argc, char* argv[]);
extern int run_TestRequestFactory(int argc, char* argv[]);
extern int run_TestDriveManager(int argc, char* argv[]);
extern int run_TestTransferState(int argc, char* argv[]);
extern int run_TestTransferManager(int argc, char* argv[]);

static int run_and_report(const char* name, int result) {
    if (result != 0) {
        fprintf(stderr, "FAIL suite: %s (exit %d)\n", name, result);
    }
    fflush(stderr);
    return result != 0 ? 1 : 0;
}

typedef int (*RunFn)(int, char**);

static char prog_name_storage[4096];

static int run_and_report(const char* name, RunFn fn, int, char**) {
    QCoreApplication::processEvents();
    char* solo_argv[] = { prog_name_storage, nullptr };
    return run_and_report(name, fn(1, solo_argv));
}

static int run_and_report(const char* name, QObject* test, int, char**) {
    QCoreApplication::processEvents();
    char* solo_argv[] = { prog_name_storage, nullptr };
    return run_and_report(name, QTest::qExec(test, 1, solo_argv));
}

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);
    snprintf(prog_name_storage, sizeof(prog_name_storage), "%s", argv[0]);
    int failures = 0;

    fprintf(stderr, "--- desktop-unit-tests: running 20 suites ---\n", failures);
    fflush(stderr);

    {
        DesktopStubTest test;
        failures += run_and_report("DesktopStubTest", &test, argc, argv);
    }

    failures += run_and_report("TestAuthServiceLogout", run_TestAuthServiceLogout, argc, argv);
    failures += run_and_report("TestAuthServiceRefresh", run_TestAuthServiceRefresh, argc, argv);
    failures += run_and_report("TestSessionStore", run_TestSessionStore, argc, argv);
    failures += run_and_report("TestOwnerSession", run_TestOwnerSession, argc, argv);
    failures += run_and_report("TestVisitorSession", run_TestVisitorSession, argc, argv);
    failures += run_and_report("TestVisitorFlow", run_TestVisitorFlow, argc, argv);
    failures += run_and_report("TestDriveItemMapping", run_TestDriveItemMapping, argc, argv);
    failures += run_and_report("TestDriveListModel", run_TestDriveListModel, argc, argv);
    failures += run_and_report("TestAdminShareListModel", run_TestAdminShareListModel, argc, argv);
    failures += run_and_report("TestErrorAdapter", run_TestErrorAdapter, argc, argv);
    failures += run_and_report("TestFolderTreeModel", run_TestFolderTreeModel, argc, argv);
    failures += run_and_report("TestUploadTaskModel", run_TestUploadTaskModel, argc, argv);
    failures += run_and_report("TestDownloadTaskModel", run_TestDownloadTaskModel, argc, argv);
    failures += run_and_report("TestAdminUserListModel", run_TestAdminUserListModel, argc, argv);
    failures += run_and_report("TestShellController", run_TestShellController, argc, argv);
    failures += run_and_report("TestRequestFactory", run_TestRequestFactory, argc, argv);
    failures += run_and_report("TestDriveManager", run_TestDriveManager, argc, argv);
    failures += run_and_report("TestTransferState", run_TestTransferState, argc, argv);
    failures += run_and_report("TestTransferManager", run_TestTransferManager, argc, argv);

    fprintf(stderr, "--- desktop-unit-tests: %d suite(s) failed ---\n", failures);
    fflush(stderr);
    return failures;
}

#include "main.moc"
