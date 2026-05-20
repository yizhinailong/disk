#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTest>

#include "helpers/MockNetworkAccessManager.hpp"
#include "managers/ProfileManager.hpp"
#include "network/NetworkClient.hpp"
#include "network/RequestFactory.hpp"

using namespace disk::desktop;
using namespace disk::desktop::managers;
using namespace disk::desktop::testing;

class TestProfileManager : public QObject {
    Q_OBJECT

private slots:

    void ChangePasswordParsesTopLevelValidationError() {
        MockNetworkAccessManager mock_network;
        mock_network.RegisterResponse(
            "api/user/password",
            QJsonObject{
                { "code", 10002 },
                { "message", "New password format error" },
                { "data", QJsonValue::Null },
            },
            400
        );

        NetworkClient network_client(static_cast<QNetworkAccessManager*>(&mock_network));
        network_client.SetBaseUrl("http://127.0.0.1:8080/");
        RequestFactory request_factory;
        request_factory.SetOwnerAccessToken("owner-token");
        ProfileManager manager(&network_client, &request_factory);

        QSignalSpy error_spy(&manager, &ProfileManager::apiError);
        QSignalSpy success_spy(&manager, &ProfileManager::operationSuccess);

        manager.changePassword("OldPassword1", "short");

        QTRY_COMPARE(error_spy.count(), 1);
        QCOMPARE(success_spy.count(), 0);

        auto arguments = error_spy.takeFirst();
        QCOMPARE(arguments.at(0).toString(), QString("New password format error"));
        QCOMPARE(arguments.at(1).toInt(), 10002);
    }

    void ChangePasswordParsesOldPasswordErrorMessage() {
        MockNetworkAccessManager mock_network;
        mock_network.RegisterResponse(
            "api/user/password",
            QJsonObject{
                { "code", 40101 },
                { "message", "Old password is incorrect" },
                { "data", QJsonValue::Null },
            },
            401
        );

        NetworkClient network_client(static_cast<QNetworkAccessManager*>(&mock_network));
        network_client.SetBaseUrl("http://127.0.0.1:8080/");
        RequestFactory request_factory;
        request_factory.SetOwnerAccessToken("owner-token");
        ProfileManager manager(&network_client, &request_factory);

        QSignalSpy error_spy(&manager, &ProfileManager::apiError);
        QSignalSpy success_spy(&manager, &ProfileManager::operationSuccess);

        manager.changePassword("WrongPassword1", "NewPassword1");

        QTRY_COMPARE(error_spy.count(), 1);
        QCOMPARE(success_spy.count(), 0);

        auto arguments = error_spy.takeFirst();
        QCOMPARE(arguments.at(0).toString(), QString("Old password is incorrect"));
        QCOMPARE(arguments.at(1).toInt(), 40101);
    }

    void ChangePasswordEmitsSuccessForSuccessEnvelope() {
        MockNetworkAccessManager mock_network;
        mock_network.RegisterResponse(
            "api/user/password",
            QJsonObject{
                { "code", 0 },
                { "message", "success" },
                { "data", QJsonValue::Null },
            }
        );

        NetworkClient network_client(static_cast<QNetworkAccessManager*>(&mock_network));
        network_client.SetBaseUrl("http://127.0.0.1:8080/");
        RequestFactory request_factory;
        request_factory.SetOwnerAccessToken("owner-token");
        ProfileManager manager(&network_client, &request_factory);

        QSignalSpy error_spy(&manager, &ProfileManager::apiError);
        QSignalSpy success_spy(&manager, &ProfileManager::operationSuccess);

        manager.changePassword("OldPassword1", "NewPassword1");

        QTRY_COMPARE(success_spy.count(), 1);
        QCOMPARE(error_spy.count(), 0);

        auto arguments = success_spy.takeFirst();
        QCOMPARE(arguments.at(0).toString(), QString::fromUtf8("密码已修改"));
    }
};

int run_TestProfileManager(int argc, char* argv[]) {
    TestProfileManager test;
    return QTest::qExec(&test, argc, argv);
}

#include "test_profile_manager.moc"
