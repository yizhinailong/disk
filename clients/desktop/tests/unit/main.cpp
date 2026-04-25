#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTest>

#include "auth/AuthService.hpp"
#include "helpers/MockNetworkAccessManager.hpp"
#include "helpers/MockReplyFactory.hpp"
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
        network_client.SetBaseUrl("http://127.0.0.1:8080/");
        RequestFactory request_factory;
        AuthService auth_service(&network_client, &request_factory);

        QSignalSpy success_spy(&auth_service, &AuthService::LoginSuccess);
        QSignalSpy failure_spy(&auth_service, &AuthService::LoginFailure);

        auth_service.Login("testuser", "Password1");

        QTRY_COMPARE(success_spy.count(), 1);
        QCOMPARE(failure_spy.count(), 0);
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

        QSignalSpy success_spy(&auth_service, &AuthService::RegisterSuccess);
        QSignalSpy failure_spy(&auth_service, &AuthService::RegisterFailure);

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

        QSignalSpy success_spy(&auth_service, &AuthService::RegisterSuccess);
        QSignalSpy failure_spy(&auth_service, &AuthService::RegisterFailure);

        auth_service.Register("newuser", "newuser@example.com", "Password1");

        QTRY_COMPARE(failure_spy.count(), 1);
        QCOMPARE(success_spy.count(), 0);

        auto arguments = failure_spy.takeFirst();
        QCOMPARE(arguments.at(0).toInt(), 40001);
        QCOMPARE(arguments.at(1).toString(), QString("用户名已存在"));
    }

    void cleanupTestCase() {
        QVERIFY(true);
    }
};

QTEST_MAIN(DesktopStubTest)
#include "main.moc"
