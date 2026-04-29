#include <QJsonArray>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTest>

#include "auth/AuthService.hpp"
#include "auth/SessionStore.hpp"
#include "auth/VisitorSessionManager.hpp"
#include "helpers/MockNetworkAccessManager.hpp"
#include "helpers/MockReplyFactory.hpp"
#include "network/NetworkClient.hpp"
#include "network/RequestFactory.hpp"

using namespace disk::desktop;
using namespace disk::desktop::testing;

class TestVisitorFlow : public QObject {
    Q_OBJECT

private slots:

    void VerifyRequestedTriggersAccessShare() {
        MockNetworkAccessManager mock_network;
        mock_network.RegisterResponse(
            "api/share/access/",
            MockReplyFactory::ShareAccessSuccessResponse()
        );

        NetworkClient network_client(static_cast<QNetworkAccessManager*>(&mock_network));
        network_client.SetBaseUrl("http://127.0.0.1:8080/");
        RequestFactory request_factory;
        AuthService auth_service(&network_client, &request_factory);
        SessionStore session_store(&network_client, &request_factory);

        auto* visitor_mgr = session_store.GetVisitorManager();

        connect(visitor_mgr, &VisitorSessionManager::verifyRequested,
                &auth_service, &AuthService::AccessShare);

        connect(&auth_service, &AuthService::shareAccessSuccess,
                visitor_mgr, &VisitorSessionManager::HandleVerifySuccess);
        connect(&auth_service, &AuthService::shareAccessFailure,
                visitor_mgr, [visitor_mgr](int error_code, const QString&) {
                    visitor_mgr->HandleVerifyFailure(error_code);
                });

        QSignalSpy established_spy(visitor_mgr, &VisitorSessionManager::sessionEstablished);
        QSignalSpy state_spy(visitor_mgr, &VisitorSessionManager::stateChanged);

        session_store.ActivateVisitor("sh_flow_test");
        QCOMPARE(visitor_mgr->GetState(), VisitorSessionState::Unverified);

        visitor_mgr->StartVerify("pass123");
        QCOMPARE(visitor_mgr->GetState(), VisitorSessionState::Verifying);

        QTRY_COMPARE(visitor_mgr->GetState(), VisitorSessionState::Active);
        QCOMPARE(visitor_mgr->GetShareToken(), QString("st_test_visitor_001"));
        QCOMPARE(visitor_mgr->GetPermission(), QString("download"));
        QCOMPARE(established_spy.count(), 1);

        QCOMPARE(mock_network.GetRequestLog().size(), 1);
        QVERIFY(mock_network.GetRequestLog().constFirst().url().toString().contains("api/share/access/sh_flow_test"));
    }

    void ReverifyRequestedTriggersAccessShare() {
        MockNetworkAccessManager mock_network;
        mock_network.RegisterResponse(
            "api/share/access/",
            MockReplyFactory::ShareAccessSuccessResponse()
        );

        NetworkClient network_client(static_cast<QNetworkAccessManager*>(&mock_network));
        network_client.SetBaseUrl("http://127.0.0.1:8080/");
        RequestFactory request_factory;
        AuthService auth_service(&network_client, &request_factory);
        SessionStore session_store(&network_client, &request_factory);

        auto* visitor_mgr = session_store.GetVisitorManager();

        connect(visitor_mgr, &VisitorSessionManager::verifyRequested,
                &auth_service, &AuthService::AccessShare);
        connect(visitor_mgr, &VisitorSessionManager::reverifyRequested,
                &auth_service, &AuthService::AccessShare);

        connect(&auth_service, &AuthService::shareAccessSuccess,
                visitor_mgr, &VisitorSessionManager::HandleVerifySuccess);
        connect(&auth_service, &AuthService::shareAccessFailure,
                visitor_mgr, [visitor_mgr](int error_code, const QString&) {
                    visitor_mgr->HandleVerifyFailure(error_code);
                });

        session_store.ActivateVisitor("sh_reverify");
        visitor_mgr->StartVerify();
        QTRY_COMPARE(visitor_mgr->GetState(), VisitorSessionState::Active);
        QCOMPARE(mock_network.GetRequestLog().size(), 1);

        visitor_mgr->HandleTokenExpired();
        QCOMPARE(visitor_mgr->GetState(), VisitorSessionState::ReverifyRequired);

        QTRY_COMPARE(visitor_mgr->GetState(), VisitorSessionState::Active);
        QCOMPARE(mock_network.GetRequestLog().size(), 2);
    }

    void ShareAccessFailurePasswordError() {
        MockNetworkAccessManager mock_network;
        mock_network.RegisterResponse(
            "api/share/access/",
            QJsonObject{
                {"code", 60003},
                {"message", "Share password error"},
                {"data", QJsonValue::Null},
            },
            403
        );

        NetworkClient network_client(static_cast<QNetworkAccessManager*>(&mock_network));
        network_client.SetBaseUrl("http://127.0.0.1:8080/");
        RequestFactory request_factory;
        AuthService auth_service(&network_client, &request_factory);
        SessionStore session_store(&network_client, &request_factory);

        auto* visitor_mgr = session_store.GetVisitorManager();

        connect(visitor_mgr, &VisitorSessionManager::verifyRequested,
                &auth_service, &AuthService::AccessShare);
        connect(&auth_service, &AuthService::shareAccessFailure,
                visitor_mgr, [visitor_mgr](int error_code, const QString&) {
                    visitor_mgr->HandleVerifyFailure(error_code);
                });

        session_store.ActivateVisitor("sh_pwd_err");
        visitor_mgr->StartVerify("wrong");
        QCOMPARE(visitor_mgr->GetState(), VisitorSessionState::Verifying);

        QTRY_COMPARE(visitor_mgr->GetState(), VisitorSessionState::Unverified);
        QVERIFY(visitor_mgr->GetShareToken().isEmpty());
    }

    void ShareAccessFailureShareExpiredClosesSession() {
        MockNetworkAccessManager mock_network;
        mock_network.RegisterResponse(
            "api/share/access/",
            QJsonObject{
                {"code", 60002},
                {"message", "Share expired"},
                {"data", QJsonValue::Null},
            },
            410
        );

        NetworkClient network_client(static_cast<QNetworkAccessManager*>(&mock_network));
        network_client.SetBaseUrl("http://127.0.0.1:8080/");
        RequestFactory request_factory;
        AuthService auth_service(&network_client, &request_factory);
        SessionStore session_store(&network_client, &request_factory);

        auto* visitor_mgr = session_store.GetVisitorManager();

        connect(visitor_mgr, &VisitorSessionManager::verifyRequested,
                &auth_service, &AuthService::AccessShare);
        connect(&auth_service, &AuthService::shareAccessFailure,
                visitor_mgr, [visitor_mgr](int error_code, const QString&) {
                    visitor_mgr->HandleVerifyFailure(error_code);
                });

        session_store.ActivateVisitor("sh_expired");
        visitor_mgr->StartVerify();
        QCOMPARE(visitor_mgr->GetState(), VisitorSessionState::Verifying);

        QTRY_COMPARE(visitor_mgr->GetState(), VisitorSessionState::Idle);
        QVERIFY(visitor_mgr->GetShareId().isEmpty());
    }

    void TokenExpiryTimerFiresHandleTokenExpired() {
        NetworkClient nc;
        RequestFactory rf;
        VisitorSessionManager mgr(&nc, &rf);

        QSignalSpy reverify_spy(&mgr, &VisitorSessionManager::reverifyRequested);

        mgr.OpenShare("sh_timer");
        mgr.StartVerify();
        QJsonObject root_files;
        mgr.HandleVerifySuccess("st_tok", 30, "view", root_files);
        QCOMPARE(mgr.GetState(), VisitorSessionState::Active);

        QTest::qWait(35000);
        QTRY_COMPARE(mgr.GetState(), VisitorSessionState::ReverifyRequired);
        QVERIFY(mgr.GetShareToken().isEmpty());
        QCOMPARE(reverify_spy.count(), 1);
    }

    void VisitorTokensDoNotContaminateOwnerTokens() {
        MockNetworkAccessManager mock_network;
        mock_network.RegisterResponse("api/auth/login", MockReplyFactory::LoginSuccessResponse());
        mock_network.RegisterResponse("api/share/access/", MockReplyFactory::ShareAccessSuccessResponse());

        NetworkClient network_client(static_cast<QNetworkAccessManager*>(&mock_network));
        network_client.SetBaseUrl("http://127.0.0.1:8080/");
        RequestFactory request_factory;
        AuthService auth_service(&network_client, &request_factory);
        SessionStore session_store(&network_client, &request_factory);

        auto* owner_mgr = session_store.GetOwnerManager();
        auto* visitor_mgr = session_store.GetVisitorManager();

        connect(&auth_service, &AuthService::loginSuccess,
                owner_mgr, &OwnerSessionManager::HandleLoginSuccess);

        connect(visitor_mgr, &VisitorSessionManager::verifyRequested,
                &auth_service, &AuthService::AccessShare);
        connect(&auth_service, &AuthService::shareAccessSuccess,
                visitor_mgr, &VisitorSessionManager::HandleVerifySuccess);
        connect(&auth_service, &AuthService::shareAccessFailure,
                visitor_mgr, [visitor_mgr](int error_code, const QString&) {
                    visitor_mgr->HandleVerifyFailure(error_code);
                });

        QSignalSpy login_spy(&auth_service, &AuthService::loginSuccess);
        auth_service.Login("testuser", "Password1");
        QTRY_COMPARE(login_spy.count(), 1);

        QCOMPARE(request_factory.GetOwnerAccessToken(), QString("eyJhbGciOiJIUzI1NiJ9.eyJ1c2VyX2lkIjoxfQ.test"));

        session_store.ActivateVisitor("sh_isolated");
        visitor_mgr->StartVerify();
        QTRY_COMPARE(visitor_mgr->GetState(), VisitorSessionState::Active);

        QCOMPARE(request_factory.GetOwnerAccessToken(), QString("eyJhbGciOiJIUzI1NiJ9.eyJ1c2VyX2lkIjoxfQ.test"));
        QCOMPARE(request_factory.GetVisitorShareToken(), QString("st_test_visitor_001"));
    }

    void ReverifyPasswordErrorReturnsToUnverified() {
        MockNetworkAccessManager mock_network;
        mock_network.RegisterResponse(
            "api/share/access/",
            MockReplyFactory::ShareAccessSuccessResponse()
        );

        NetworkClient network_client(static_cast<QNetworkAccessManager*>(&mock_network));
        network_client.SetBaseUrl("http://127.0.0.1:8080/");
        RequestFactory request_factory;
        AuthService auth_service(&network_client, &request_factory);
        SessionStore session_store(&network_client, &request_factory);

        auto* visitor_mgr = session_store.GetVisitorManager();

        connect(visitor_mgr, &VisitorSessionManager::verifyRequested,
                &auth_service, &AuthService::AccessShare);
        connect(visitor_mgr, &VisitorSessionManager::reverifyRequested,
                &auth_service, &AuthService::AccessShare);
        connect(&auth_service, &AuthService::shareAccessSuccess,
                visitor_mgr, &VisitorSessionManager::HandleVerifySuccess);
        connect(&auth_service, &AuthService::shareAccessFailure,
                visitor_mgr, [visitor_mgr](int error_code, const QString&) {
                    visitor_mgr->HandleVerifyFailure(error_code);
                });

        QSignalSpy state_spy(visitor_mgr, &VisitorSessionManager::stateChanged);

        session_store.ActivateVisitor("sh_reverify_pwd");
        visitor_mgr->StartVerify("pass123");
        QTRY_COMPARE(visitor_mgr->GetState(), VisitorSessionState::Active);
        QCOMPARE(mock_network.GetRequestLog().size(), 1);

        mock_network.Clear();
        mock_network.RegisterResponse(
            "api/share/access/",
            QJsonObject{
                {"code", 60003},
                {"message", "Share password error"},
                {"data", QJsonValue::Null},
            },
            403
        );

        visitor_mgr->HandleTokenExpired();
        QCOMPARE(visitor_mgr->GetState(), VisitorSessionState::ReverifyRequired);

        QTRY_COMPARE(visitor_mgr->GetState(), VisitorSessionState::Unverified);
        QVERIFY(visitor_mgr->GetShareToken().isEmpty());
        QCOMPARE(mock_network.GetRequestLog().size(), 1);
    }
};

int run_TestVisitorFlow(int argc, char* argv[]) {
    TestVisitorFlow test;
    return QTest::qExec(&test, argc, argv);
}

#include "test_visitor_flow.moc"
