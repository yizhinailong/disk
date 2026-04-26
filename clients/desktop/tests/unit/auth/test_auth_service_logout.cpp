#include <QJsonDocument>
#include <QFile>
#include <QSignalSpy>
#include <QTest>

#include "auth/AuthService.hpp"
#include "helpers/MockNetworkAccessManager.hpp"
#include "helpers/MockReplyFactory.hpp"
#include "network/NetworkClient.hpp"
#include "network/RequestFactory.hpp"

using namespace disk::desktop;
using namespace disk::desktop::testing;

class TestAuthServiceLogout : public QObject {
    Q_OBJECT

private slots:

    void LogoutSendsBearerHeader() {
        MockNetworkAccessManager mock_network;
        mock_network.RegisterRawResponse("api/auth/logout", "{}", 200);

        NetworkClient network_client(static_cast<QNetworkAccessManager*>(&mock_network));
        network_client.SetBaseUrl("http://127.0.0.1:8080/");
        RequestFactory request_factory;
        AuthService auth_service(&network_client, &request_factory);

        QSignalSpy success_spy(&auth_service, &AuthService::logoutSuccess);

        auth_service.Logout("test_access_token");

        QTRY_COMPARE(success_spy.count(), 1);
        QCOMPARE(mock_network.GetRequestLog().size(), 1);

        auto request = mock_network.GetRequestLog().constFirst();
        QCOMPARE(request.url().toString(), QString("http://127.0.0.1:8080/api/auth/logout"));
        QCOMPARE(
            request.rawHeader("Authorization"),
            QByteArray("Bearer test_access_token")
        );
    }

    void LogoutSuccessOn200WithEmptyBody() {
        MockNetworkAccessManager mock_network;
        mock_network.RegisterRawResponse("api/auth/logout", QByteArray(), 200);

        NetworkClient network_client(static_cast<QNetworkAccessManager*>(&mock_network));
        network_client.SetBaseUrl("http://127.0.0.1:8080/");
        RequestFactory request_factory;
        AuthService auth_service(&network_client, &request_factory);

        QSignalSpy success_spy(&auth_service, &AuthService::logoutSuccess);
        QSignalSpy failure_spy(&auth_service, &AuthService::logoutFailure);

        auth_service.Logout("any_token");

        QTRY_COMPARE(success_spy.count(), 1);
        QCOMPARE(failure_spy.count(), 0);
    }

    void LogoutSuccessOn200WithValidJson() {
        MockNetworkAccessManager mock_network;
        mock_network.RegisterRawResponse(
            "api/auth/logout",
            R"({"code":0,"message":"success","data":null})",
            200
        );

        NetworkClient network_client(static_cast<QNetworkAccessManager*>(&mock_network));
        network_client.SetBaseUrl("http://127.0.0.1:8080/");
        RequestFactory request_factory;
        AuthService auth_service(&network_client, &request_factory);

        QSignalSpy success_spy(&auth_service, &AuthService::logoutSuccess);
        QSignalSpy failure_spy(&auth_service, &AuthService::logoutFailure);

        auth_service.Logout("token");

        QTRY_COMPARE(success_spy.count(), 1);
        QCOMPARE(failure_spy.count(), 0);
    }

    void LogoutRequestIsHttpPost() {
        MockNetworkAccessManager mock_network;
        mock_network.RegisterRawResponse("api/auth/logout", "{}", 200);

        NetworkClient network_client(static_cast<QNetworkAccessManager*>(&mock_network));
        network_client.SetBaseUrl("http://127.0.0.1:8080/");
        RequestFactory request_factory;
        AuthService auth_service(&network_client, &request_factory);

        auth_service.Logout("token");

        QTRY_COMPARE(mock_network.GetRequestLog().size(), 1);
        QCOMPARE(mock_network.GetRequestBodyLog().size(), 1);
        QVERIFY(mock_network.GetRequestBodyLog().constFirst().isEmpty());
    }

    void LogoutSuccessOnNetworkErrorWithoutBackendError() {
        MockNetworkAccessManager mock_network;
        mock_network.RegisterError(
            "api/auth/logout",
            QNetworkReply::ConnectionRefusedError,
            "Connection refused"
        );

        NetworkClient network_client(static_cast<QNetworkAccessManager*>(&mock_network));
        network_client.SetBaseUrl("http://127.0.0.1:8080/");
        RequestFactory request_factory;
        AuthService auth_service(&network_client, &request_factory);

        QSignalSpy success_spy(&auth_service, &AuthService::logoutSuccess);
        QSignalSpy failure_spy(&auth_service, &AuthService::logoutFailure);

        auth_service.Logout("token");

        QTRY_COMPARE(success_spy.count(), 1);
        QCOMPARE(failure_spy.count(), 0);
    }

    void LogoutFailureOnBackendApiError() {
        MockNetworkAccessManager mock_network;
        mock_network.RegisterResponse(
            "api/auth/logout",
            QJsonObject{
                { "code", 40106 },
                { "message", "Token missing" },
                { "data", QJsonValue::Null },
            },
            401
        );

        NetworkClient network_client(static_cast<QNetworkAccessManager*>(&mock_network));
        network_client.SetBaseUrl("http://127.0.0.1:8080/");
        RequestFactory request_factory;
        AuthService auth_service(&network_client, &request_factory);

        QSignalSpy success_spy(&auth_service, &AuthService::logoutSuccess);
        QSignalSpy failure_spy(&auth_service, &AuthService::logoutFailure);

        auth_service.Logout("bad_token");

        QTRY_COMPARE(failure_spy.count(), 1);
        QCOMPARE(success_spy.count(), 0);
    }

    void LogoutFailureOn2xxWithApiError() {
        MockNetworkAccessManager mock_network;
        mock_network.RegisterResponse(
            "api/auth/logout",
            QJsonObject{
                { "code", 50001 },
                { "message", "Internal error" },
                { "data", QJsonValue::Null },
            },
            200
        );

        NetworkClient network_client(static_cast<QNetworkAccessManager*>(&mock_network));
        network_client.SetBaseUrl("http://127.0.0.1:8080/");
        RequestFactory request_factory;
        AuthService auth_service(&network_client, &request_factory);

        QSignalSpy success_spy(&auth_service, &AuthService::logoutSuccess);
        QSignalSpy failure_spy(&auth_service, &AuthService::logoutFailure);

        auth_service.Logout("token");

        QTRY_COMPARE(failure_spy.count(), 1);
        QCOMPARE(success_spy.count(), 0);
    }

    void LogoutWithEmptyTokenSendsEmptyBearer() {
        MockNetworkAccessManager mock_network;
        mock_network.RegisterRawResponse("api/auth/logout", "{}", 200);

        NetworkClient network_client(static_cast<QNetworkAccessManager*>(&mock_network));
        network_client.SetBaseUrl("http://127.0.0.1:8080/");
        RequestFactory request_factory;
        AuthService auth_service(&network_client, &request_factory);

        QSignalSpy success_spy(&auth_service, &AuthService::logoutSuccess);

        auth_service.Logout("");

        QTRY_COMPARE(success_spy.count(), 1);

        auto request = mock_network.GetRequestLog().constFirst();
        QCOMPARE(request.rawHeader("Authorization"), QByteArray("Bearer"));
    }

    void ApplicationShutsDownTransfersWhenLogoutBegins() {
        QFile application_source(QStringLiteral(QT_TEST_SOURCE_DIR "/../src/app/Application.cpp"));
        QVERIFY(application_source.open(QIODevice::ReadOnly));

        const auto source = QString::fromUtf8(application_source.readAll());
        QVERIFY(source.contains("OwnerSessionState::LogoutPending"));
        QVERIFY(source.contains("m_transfer_manager->ShutdownOwnerTransfers();"));
    }
};

int run_TestAuthServiceLogout(int argc, char* argv[]) {
    TestAuthServiceLogout test;
    return QTest::qExec(&test, argc, argv);
}

#include "test_auth_service_logout.moc"
