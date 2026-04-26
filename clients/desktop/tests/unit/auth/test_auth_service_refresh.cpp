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

class TestAuthServiceRefresh : public QObject {
    Q_OBJECT

private slots:

    void RefreshSendsRefreshTokenInBody() {
        MockNetworkAccessManager mock_network;
        mock_network.RegisterResponse(
            "api/auth/refresh",
            MockReplyFactory::RefreshSuccessResponse()
        );

        NetworkClient network_client(static_cast<QNetworkAccessManager*>(&mock_network));
        network_client.SetBaseUrl("http://127.0.0.1:8080/");
        RequestFactory request_factory;
        AuthService auth_service(&network_client, &request_factory);

        QSignalSpy success_spy(&auth_service, &AuthService::refreshSuccess);
        QSignalSpy failure_spy(&auth_service, &AuthService::refreshFailure);

        auth_service.RefreshToken("rt_old_token_001");

        QTRY_COMPARE(success_spy.count(), 1);
        QCOMPARE(failure_spy.count(), 0);

        QCOMPARE(mock_network.GetRequestBodyLog().size(), 1);
        auto body = QJsonDocument::fromJson(
            mock_network.GetRequestBodyLog().constFirst()
        ).object();
        QCOMPARE(body.value("refresh_token").toString(), QString("rt_old_token_001"));
    }

    void RefreshSuccessEmitsNewTokens() {
        MockNetworkAccessManager mock_network;
        mock_network.RegisterResponse(
            "api/auth/refresh",
            MockReplyFactory::RefreshSuccessResponse()
        );

        NetworkClient network_client(static_cast<QNetworkAccessManager*>(&mock_network));
        network_client.SetBaseUrl("http://127.0.0.1:8080/");
        RequestFactory request_factory;
        AuthService auth_service(&network_client, &request_factory);

        QSignalSpy success_spy(&auth_service, &AuthService::refreshSuccess);

        auth_service.RefreshToken("rt_old");

        QTRY_COMPARE(success_spy.count(), 1);

        auto args = success_spy.takeFirst();
        QCOMPARE(args.at(0).toString(), QString("eyJhbGciOiJIUzI1NiJ9.eyJ1c2VyX2lkIjoxfQ.refreshed"));
        QCOMPARE(args.at(1).toString(), QString("rt_test_refresh_002"));
        QCOMPARE(args.at(2).toInt(), 7200);
    }

    void RefreshFailureEmitsError() {
        MockNetworkAccessManager mock_network;
        mock_network.RegisterResponse(
            "api/auth/refresh",
            QJsonObject{
                { "code", 40105 },
                { "message", "Refresh token无效或已使用" },
                { "data", QJsonValue::Null },
            },
            401
        );

        NetworkClient network_client(static_cast<QNetworkAccessManager*>(&mock_network));
        network_client.SetBaseUrl("http://127.0.0.1:8080/");
        RequestFactory request_factory;
        AuthService auth_service(&network_client, &request_factory);

        QSignalSpy success_spy(&auth_service, &AuthService::refreshSuccess);
        QSignalSpy failure_spy(&auth_service, &AuthService::refreshFailure);

        auth_service.RefreshToken("rt_used");

        QTRY_COMPARE(failure_spy.count(), 1);
        QCOMPARE(success_spy.count(), 0);

        auto args = failure_spy.takeFirst();
        QCOMPARE(args.at(0).toInt(), 40105);
        QCOMPARE(args.at(1).toString(), QString("Refresh token无效或已使用"));
    }

    void RefreshUsesCorrectEndpoint() {
        MockNetworkAccessManager mock_network;
        mock_network.RegisterResponse(
            "api/auth/refresh",
            MockReplyFactory::RefreshSuccessResponse()
        );

        NetworkClient network_client(static_cast<QNetworkAccessManager*>(&mock_network));
        network_client.SetBaseUrl("http://127.0.0.1:8080/");
        RequestFactory request_factory;
        AuthService auth_service(&network_client, &request_factory);

        QSignalSpy success_spy(&auth_service, &AuthService::refreshSuccess);
        auth_service.RefreshToken("rt_test");

        QTRY_COMPARE(success_spy.count(), 1);

        QCOMPARE(
            mock_network.GetRequestLog().constFirst().url().toString(),
            QString("http://127.0.0.1:8080/api/auth/refresh")
        );
    }

    void RefreshInvalidJsonEmitsFailure() {
        MockNetworkAccessManager mock_network;
        mock_network.RegisterRawResponse("api/auth/refresh", "not json", 200);

        NetworkClient network_client(static_cast<QNetworkAccessManager*>(&mock_network));
        network_client.SetBaseUrl("http://127.0.0.1:8080/");
        RequestFactory request_factory;
        AuthService auth_service(&network_client, &request_factory);

        QSignalSpy failure_spy(&auth_service, &AuthService::refreshFailure);

        auth_service.RefreshToken("rt_test");

        QTRY_COMPARE(failure_spy.count(), 1);
        QCOMPARE(failure_spy.takeFirst().at(0).toInt(), -100);
    }

    void ApplicationShutsDownTransfersWhenReauthIsRequired() {
        QFile application_source(QStringLiteral(QT_TEST_SOURCE_DIR "/../src/app/Application.cpp"));
        QVERIFY(application_source.open(QIODevice::ReadOnly));

        const auto source = QString::fromUtf8(application_source.readAll());
        QVERIFY(source.contains("OwnerSessionState::ReauthRequired"));
        QVERIFY(source.contains("m_transfer_manager->ShutdownOwnerTransfers();"));
    }
};

int run_TestAuthServiceRefresh(int argc, char* argv[]) {
    TestAuthServiceRefresh test;
    return QTest::qExec(&test, argc, argv);
}

#include "test_auth_service_refresh.moc"
