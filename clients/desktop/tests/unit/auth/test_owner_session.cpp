#include <QJsonObject>
#include <QSignalSpy>
#include <QTest>

#include "auth/OwnerSessionManager.hpp"
#include "network/NetworkClient.hpp"
#include "network/RequestFactory.hpp"

using namespace disk::desktop;

class TestOwnerSession : public QObject {
    Q_OBJECT

private slots:

    void InitStateIsLoggedOut() {
        NetworkClient nc;
        RequestFactory rf;
        OwnerSessionManager mgr(&nc, &rf);

        QCOMPARE(mgr.GetState(), OwnerSessionState::LoggedOut);
        QVERIFY(mgr.GetAccessToken().isEmpty());
        QVERIFY(mgr.GetRefreshToken().isEmpty());
        QCOMPARE(mgr.GetUserId(), quint64(0));
        QVERIFY(mgr.GetUsername().isEmpty());
    }

    void LoginSuccessTransition() {
        NetworkClient nc;
        RequestFactory rf;
        OwnerSessionManager mgr(&nc, &rf);
        QSignalSpy state_spy(&mgr, &OwnerSessionManager::stateChanged);
        QSignalSpy tokens_spy(&mgr, &OwnerSessionManager::tokensUpdated);

        mgr.StartLogin();
        QCOMPARE(mgr.GetState(), OwnerSessionState::Authenticating);
        QCOMPARE(state_spy.count(), 1);

        QJsonObject user;
        user["id"] = 42;
        user["username"] = "alice";
        mgr.HandleLoginSuccess("access_abc", "refresh_xyz", 7200, user);

        QCOMPARE(mgr.GetState(), OwnerSessionState::Active);
        QCOMPARE(mgr.GetAccessToken(), QString("access_abc"));
        QCOMPARE(mgr.GetRefreshToken(), QString("refresh_xyz"));
        QCOMPARE(mgr.GetUserId(), quint64(42));
        QCOMPARE(mgr.GetUsername(), QString("alice"));
        QCOMPARE(state_spy.count(), 2);
        QCOMPARE(tokens_spy.count(), 1);
    }

    void LoginFailureStaysLoggedOut() {
        NetworkClient nc;
        RequestFactory rf;
        OwnerSessionManager mgr(&nc, &rf);
        QSignalSpy state_spy(&mgr, &OwnerSessionManager::stateChanged);

        mgr.StartLogin();
        QCOMPARE(mgr.GetState(), OwnerSessionState::Authenticating);

        mgr.HandleLoginFailure();
        QCOMPARE(mgr.GetState(), OwnerSessionState::LoggedOut);
        QVERIFY(mgr.GetAccessToken().isEmpty());
        QCOMPARE(state_spy.count(), 2);
    }

    void RefreshSuccessTransition() {
        NetworkClient nc;
        RequestFactory rf;
        OwnerSessionManager mgr(&nc, &rf);

        QJsonObject user;
        user["id"] = 1;
        user["username"] = "bob";
        mgr.StartLogin();
        mgr.HandleLoginSuccess("old_access", "old_refresh", 7200, user);
        QCOMPARE(mgr.GetState(), OwnerSessionState::Active);

        QSignalSpy state_spy(&mgr, &OwnerSessionManager::stateChanged);
        QSignalSpy refresh_spy(&mgr, &OwnerSessionManager::refreshRequested);

        mgr.HandleTokenExpired();
        QCOMPARE(mgr.GetState(), OwnerSessionState::Refreshing);
        QCOMPARE(refresh_spy.count(), 1);
        QCOMPARE(refresh_spy.takeFirst().at(0).toString(), QString("old_refresh"));

        mgr.HandleRefreshSuccess("new_access", "new_refresh", 7200);
        QCOMPARE(mgr.GetState(), OwnerSessionState::Active);
        QCOMPARE(mgr.GetAccessToken(), QString("new_access"));
        QCOMPARE(mgr.GetRefreshToken(), QString("new_refresh"));
    }

    void RefreshFailureTransitionsToReauth() {
        NetworkClient nc;
        RequestFactory rf;
        OwnerSessionManager mgr(&nc, &rf);

        QJsonObject user;
        user["id"] = 1;
        user["username"] = "bob";
        mgr.StartLogin();
        mgr.HandleLoginSuccess("access", "refresh", 7200, user);

        QSignalSpy login_spy(&mgr, &OwnerSessionManager::loginRequired);

        mgr.HandleTokenExpired();
        QCOMPARE(mgr.GetState(), OwnerSessionState::Refreshing);

        mgr.HandleRefreshFailure();
        QCOMPARE(mgr.GetState(), OwnerSessionState::ReauthRequired);
        QVERIFY(mgr.GetAccessToken().isEmpty());
        QVERIFY(mgr.GetRefreshToken().isEmpty());
        QCOMPARE(login_spy.count(), 1);
    }

    void LogoutTransition() {
        NetworkClient nc;
        RequestFactory rf;
        OwnerSessionManager mgr(&nc, &rf);

        QJsonObject user;
        user["id"] = 1;
        user["username"] = "bob";
        mgr.StartLogin();
        mgr.HandleLoginSuccess("access", "refresh", 7200, user);
        QCOMPARE(mgr.GetState(), OwnerSessionState::Active);

        QSignalSpy logout_spy(&mgr, &OwnerSessionManager::logoutRequested);
        QSignalSpy cleared_spy(&mgr, &OwnerSessionManager::sessionCleared);

        mgr.StartLogout();
        QCOMPARE(mgr.GetState(), OwnerSessionState::LogoutPending);
        QCOMPARE(logout_spy.count(), 1);
        QCOMPARE(logout_spy.takeFirst().at(0).toString(), QString("access"));

        mgr.CompleteLogout();
        QCOMPARE(mgr.GetState(), OwnerSessionState::LoggedOut);
        QVERIFY(mgr.GetAccessToken().isEmpty());
        QCOMPARE(cleared_spy.count(), 1);
    }

    void MaxReplayAllowsOneRetry() {
        NetworkClient nc;
        RequestFactory rf;
        OwnerSessionManager mgr(&nc, &rf);

        QVERIFY(mgr.ShouldRetryAfterRefresh());
    }

    void ShouldRetryAfterRefreshEnforcement() {
        NetworkClient nc;
        RequestFactory rf;
        OwnerSessionManager mgr(&nc, &rf);

        QJsonObject user;
        user["id"] = 1;
        user["username"] = "bob";
        mgr.StartLogin();
        mgr.HandleLoginSuccess("access", "refresh", 7200, user);

        QVERIFY(mgr.ShouldRetryAfterRefresh());

        mgr.HandleTokenExpired();
        mgr.HandleRefreshSuccess("new_access", "new_refresh", 7200);

        QVERIFY(!mgr.ShouldRetryAfterRefresh());
    }

    void TokenExpiredWhileRefreshingIsNoop() {
        NetworkClient nc;
        RequestFactory rf;
        OwnerSessionManager mgr(&nc, &rf);

        QJsonObject user;
        user["id"] = 1;
        user["username"] = "bob";
        mgr.StartLogin();
        mgr.HandleLoginSuccess("access", "refresh", 7200, user);

        mgr.HandleTokenExpired();
        QCOMPARE(mgr.GetState(), OwnerSessionState::Refreshing);

        QSignalSpy state_spy(&mgr, &OwnerSessionManager::stateChanged);
        mgr.HandleTokenExpired();
        QCOMPARE(state_spy.count(), 0);
        QCOMPARE(mgr.GetState(), OwnerSessionState::Refreshing);
    }

    void TokenExpiredWhileLogoutPendingIsNoop() {
        NetworkClient nc;
        RequestFactory rf;
        OwnerSessionManager mgr(&nc, &rf);

        QJsonObject user;
        user["id"] = 1;
        user["username"] = "bob";
        mgr.StartLogin();
        mgr.HandleLoginSuccess("access", "refresh", 7200, user);

        mgr.StartLogout();
        QCOMPARE(mgr.GetState(), OwnerSessionState::LogoutPending);

        QSignalSpy state_spy(&mgr, &OwnerSessionManager::stateChanged);
        mgr.HandleTokenExpired();
        QCOMPARE(state_spy.count(), 0);
        QCOMPARE(mgr.GetState(), OwnerSessionState::LogoutPending);
    }

    void TokenExpiredWithEmptyRefreshTokenGoesReauth() {
        NetworkClient nc;
        RequestFactory rf;
        OwnerSessionManager mgr(&nc, &rf);

        QJsonObject user;
        user["id"] = 1;
        user["username"] = "bob";
        mgr.StartLogin();
        mgr.HandleLoginSuccess("access", "", 7200, user);

        QSignalSpy login_spy(&mgr, &OwnerSessionManager::loginRequired);
        mgr.HandleTokenExpired();

        QCOMPARE(mgr.GetState(), OwnerSessionState::ReauthRequired);
        QCOMPARE(login_spy.count(), 1);
    }

    void RoleExtractedFromAdminJwt() {
        NetworkClient nc;
        RequestFactory rf;
        OwnerSessionManager mgr(&nc, &rf);
        QSignalSpy role_spy(&mgr, &OwnerSessionManager::roleChanged);

        // Construct a JWT with role=1: header.payload.signature
        const QByteArray payload = QByteArray("{\"role\":1}").toBase64();
        const QString admin_token = QString("header.") + payload + ".signature";

        QJsonObject user;
        user["id"] = 1;
        user["username"] = "admin";
        mgr.StartLogin();
        mgr.HandleLoginSuccess(admin_token, "refresh", 7200, user);

        QCOMPARE(mgr.GetRole(), 1);
        QCOMPARE(role_spy.count(), 1);
    }

    void RoleExtractedFromRegularUserJwt() {
        NetworkClient nc;
        RequestFactory rf;
        OwnerSessionManager mgr(&nc, &rf);
        QSignalSpy role_spy(&mgr, &OwnerSessionManager::roleChanged);

        const QByteArray payload = QByteArray("{\"role\":0}").toBase64();
        const QString user_token = QString("header.") + payload + ".signature";

        QJsonObject user;
        user["id"] = 2;
        user["username"] = "regular";
        mgr.StartLogin();
        mgr.HandleLoginSuccess(user_token, "refresh", 7200, user);

        QCOMPARE(mgr.GetRole(), 0);
        // roleChanged should not fire when role stays 0
        QCOMPARE(role_spy.count(), 0);
    }

    void RoleDefaultsToZeroForMalformedJwt() {
        NetworkClient nc;
        RequestFactory rf;
        OwnerSessionManager mgr(&nc, &rf);
        QSignalSpy role_spy(&mgr, &OwnerSessionManager::roleChanged);

        // Empty token
        QJsonObject user;
        user["id"] = 3;
        user["username"] = "bob";
        mgr.StartLogin();
        mgr.HandleLoginSuccess("", "refresh", 7200, user);

        QCOMPARE(mgr.GetRole(), 0);
        // roleChanged should not fire when role stays 0
        QCOMPARE(role_spy.count(), 0);
    }

    void RoleUpdatedOnRefresh() {
        NetworkClient nc;
        RequestFactory rf;
        OwnerSessionManager mgr(&nc, &rf);
        QSignalSpy role_spy(&mgr, &OwnerSessionManager::roleChanged);

        // Login with role=0
        const QByteArray payload0 = QByteArray("{\"role\":0}").toBase64();
        const QString token0 = QString("header.") + payload0 + ".signature";

        QJsonObject user;
        user["id"] = 1;
        user["username"] = "bob";
        mgr.StartLogin();
        mgr.HandleLoginSuccess(token0, "refresh", 7200, user);
        QCOMPARE(mgr.GetRole(), 0);
        role_spy.clear();

        // Refresh with role=1
        mgr.HandleTokenExpired();
        const QByteArray payload1 = QByteArray("{\"role\":1}").toBase64();
        const QString token1 = QString("header.") + payload1 + ".signature";
        mgr.HandleRefreshSuccess(token1, "new_refresh", 7200);

        QCOMPARE(mgr.GetRole(), 1);
        // roleChanged should fire exactly once on transition from 0->1
        QVERIFY(role_spy.count() == 1);
    }
};

int run_TestOwnerSession(int argc, char* argv[]) {
    TestOwnerSession test;
    return QTest::qExec(&test, argc, argv);
}

#include "test_owner_session.moc"
