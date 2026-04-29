#include <QSignalSpy>
#include <QTest>

#include "app/ShellController.hpp"
#include "auth/SessionStore.hpp"
#include "network/NetworkClient.hpp"
#include "network/RequestFactory.hpp"

using namespace disk::desktop;
using namespace disk::app;

class TestShellController : public QObject {
    Q_OBJECT

private slots:

    void InitDefaultsToSplash() {
        NetworkClient nc;
        RequestFactory rf;
        SessionStore store(&nc, &rf);
        ShellController ctrl(&store);

        QCOMPARE(ctrl.GetCurrentShell(), QString("splash"));
        QCOMPARE(ctrl.GetPageState(), QString("loading"));
    }

    void UnauthenticatedNavigatesToLogin() {
        NetworkClient nc;
        RequestFactory rf;
        SessionStore store(&nc, &rf);
        ShellController ctrl(&store);

        ctrl.Initialize();

        QCOMPARE(ctrl.GetCurrentShell(), QString("login"));
    }

    void AuthenticatedNavigatesToOwner() {
        NetworkClient nc;
        RequestFactory rf;
        SessionStore store(&nc, &rf);
        ShellController ctrl(&store);

        QJsonObject user;
        user["id"] = 1;
        user["username"] = "alice";
        store.GetOwnerManager()->StartLogin();
        store.GetOwnerManager()->HandleLoginSuccess("access", "refresh", 7200, user);

        ctrl.Initialize();

        QCOMPARE(ctrl.GetCurrentShell(), QString("owner"));
    }

    void OwnerActiveNavigateToOwnerAllowed() {
        NetworkClient nc;
        RequestFactory rf;
        SessionStore store(&nc, &rf);
        ShellController ctrl(&store);

        QJsonObject user;
        user["id"] = 1;
        user["username"] = "alice";
        store.GetOwnerManager()->StartLogin();
        store.GetOwnerManager()->HandleLoginSuccess("access", "refresh", 7200, user);

        ctrl.navigateToOwner();
        QCOMPARE(ctrl.GetCurrentShell(), QString("owner"));
    }

    void UnauthenticatedNavigateToOwnerRedirectsToLogin() {
        NetworkClient nc;
        RequestFactory rf;
        SessionStore store(&nc, &rf);
        ShellController ctrl(&store);

        ctrl.navigateToOwner();

        QCOMPARE(ctrl.GetCurrentShell(), QString("login"));
    }

    void NavigateToVisitorSetsShell() {
        NetworkClient nc;
        RequestFactory rf;
        SessionStore store(&nc, &rf);
        ShellController ctrl(&store);

        ctrl.navigateToVisitor("sh_test");

        QCOMPARE(ctrl.GetCurrentShell(), QString("visitor"));
    }

    void NavigateToLoginFromOwner() {
        NetworkClient nc;
        RequestFactory rf;
        SessionStore store(&nc, &rf);
        ShellController ctrl(&store);

        QJsonObject user;
        user["id"] = 1;
        user["username"] = "alice";
        store.GetOwnerManager()->StartLogin();
        store.GetOwnerManager()->HandleLoginSuccess("access", "refresh", 7200, user);

        ctrl.Initialize();
        QCOMPARE(ctrl.GetCurrentShell(), QString("owner"));

        ctrl.navigateToLogin();
        QCOMPARE(ctrl.GetCurrentShell(), QString("login"));
    }

    void NavigateToRegister() {
        NetworkClient nc;
        RequestFactory rf;
        SessionStore store(&nc, &rf);
        ShellController ctrl(&store);

        ctrl.navigateToRegister();

        QCOMPARE(ctrl.GetCurrentShell(), QString("register"));
    }

    void LogoutPendingRedirectsToLogin() {
        NetworkClient nc;
        RequestFactory rf;
        SessionStore store(&nc, &rf);
        ShellController ctrl(&store);

        QJsonObject user;
        user["id"] = 1;
        user["username"] = "alice";
        store.GetOwnerManager()->StartLogin();
        store.GetOwnerManager()->HandleLoginSuccess("access", "refresh", 7200, user);

        ctrl.Initialize();
        QCOMPARE(ctrl.GetCurrentShell(), QString("owner"));

        store.GetOwnerManager()->StartLogout();

        QCOMPARE(store.GetOwnerManager()->GetState(), OwnerSessionState::LogoutPending);

        store.GetOwnerManager()->CompleteLogout();

        QCOMPARE(store.GetOwnerManager()->GetState(), OwnerSessionState::LoggedOut);
        QCOMPARE(ctrl.GetCurrentShell(), QString("login"));
    }

    void RefreshingAllowsOwnerNavigation() {
        NetworkClient nc;
        RequestFactory rf;
        SessionStore store(&nc, &rf);
        ShellController ctrl(&store);

        QJsonObject user;
        user["id"] = 1;
        user["username"] = "alice";
        store.GetOwnerManager()->StartLogin();
        store.GetOwnerManager()->HandleLoginSuccess("access", "refresh", 7200, user);

        ctrl.Initialize();
        QCOMPARE(ctrl.GetCurrentShell(), QString("owner"));

        store.GetOwnerManager()->HandleTokenExpired();
        QCOMPARE(store.GetOwnerManager()->GetState(), OwnerSessionState::Refreshing);

        ctrl.navigateToOwner();
        QCOMPARE(ctrl.GetCurrentShell(), QString("owner"));
    }

    void ReauthRequiredRedirectsToLogin() {
        NetworkClient nc;
        RequestFactory rf;
        SessionStore store(&nc, &rf);
        ShellController ctrl(&store);

        QJsonObject user;
        user["id"] = 1;
        user["username"] = "alice";
        store.GetOwnerManager()->StartLogin();
        store.GetOwnerManager()->HandleLoginSuccess("access", "refresh", 7200, user);

        ctrl.Initialize();
        QCOMPARE(ctrl.GetCurrentShell(), QString("owner"));

        store.GetOwnerManager()->HandleTokenExpired();
        QCOMPARE(store.GetOwnerManager()->GetState(), OwnerSessionState::Refreshing);

        store.GetOwnerManager()->HandleRefreshFailure();
        QCOMPARE(store.GetOwnerManager()->GetState(), OwnerSessionState::ReauthRequired);
        QCOMPARE(ctrl.GetCurrentShell(), QString("login"));
    }

    void ReauthRequiredClearsTokens() {
        NetworkClient nc;
        RequestFactory rf;
        SessionStore store(&nc, &rf);

        QJsonObject user;
        user["id"] = 1;
        user["username"] = "alice";
        store.GetOwnerManager()->StartLogin();
        store.GetOwnerManager()->HandleLoginSuccess("access", "refresh", 7200, user);
        QCOMPARE(rf.GetOwnerAccessToken(), QString("access"));

        store.GetOwnerManager()->HandleTokenExpired();
        store.GetOwnerManager()->HandleRefreshFailure();

        QVERIFY(store.GetOwnerManager()->GetAccessToken().isEmpty());
        QVERIFY(store.GetOwnerManager()->GetRefreshToken().isEmpty());
        QVERIFY(rf.GetOwnerAccessToken().isEmpty());
    }

    void SetPageStateEmitsSignal() {
        NetworkClient nc;
        RequestFactory rf;
        SessionStore store(&nc, &rf);
        ShellController ctrl(&store);
        QSignalSpy spy(&ctrl, &ShellController::pageStateChanged);

        ctrl.setPageState("content");
        QCOMPARE(ctrl.GetPageState(), QString("content"));
        QCOMPARE(spy.count(), 1);
    }

    void NullSessionStoreIsSafe() {
        ShellController ctrl(nullptr);

        ctrl.Initialize();
        ctrl.navigateToOwner();
        ctrl.navigateToVisitor("sh_test");
        ctrl.navigateToLogin();
        ctrl.navigateToSplash();

        QCOMPARE(ctrl.GetCurrentShell(), QString("splash"));
    }

    // ── Task 11: Auth / Visitor route guard regressions ────────────────────

    void VisitorReverifyRequiredStaysOnVisitor() {
        NetworkClient nc;
        RequestFactory rf;
        SessionStore store(&nc, &rf);
        ShellController ctrl(&store);

        ctrl.navigateToVisitor("sh_reverify");
        QCOMPARE(ctrl.GetCurrentShell(), QString("visitor"));

        // Transition visitor to Active, then expire the token
        store.GetVisitorManager()->OpenShare("sh_reverify");
        store.GetVisitorManager()->StartVerify();
        QJsonObject root_files;
        store.GetVisitorManager()->HandleVerifySuccess("st_tok", 3600, "view", root_files);
        QCOMPARE(store.GetVisitorManager()->GetState(), VisitorSessionState::Active);

        store.GetVisitorManager()->HandleTokenExpired();
        QCOMPARE(store.GetVisitorManager()->GetState(), VisitorSessionState::ReverifyRequired);

        // Shell must stay on visitor, not redirect to owner or login
        QCOMPARE(ctrl.GetCurrentShell(), QString("visitor"));
    }

    void VisitorReverifyRequiredSetsLoadingState() {
        NetworkClient nc;
        RequestFactory rf;
        SessionStore store(&nc, &rf);
        ShellController ctrl(&store);

        ctrl.navigateToVisitor("sh_reverify");
        store.GetVisitorManager()->OpenShare("sh_reverify");
        store.GetVisitorManager()->StartVerify();
        QJsonObject root_files;
        store.GetVisitorManager()->HandleVerifySuccess("st_tok", 3600, "view", root_files);

        store.GetVisitorManager()->HandleTokenExpired();
        QCOMPARE(ctrl.GetPageState(), QString("loading"));
    }

    void VisitorVerifyingSetsLoadingState() {
        NetworkClient nc;
        RequestFactory rf;
        SessionStore store(&nc, &rf);
        ShellController ctrl(&store);

        ctrl.navigateToVisitor("sh_verify");
        store.GetVisitorManager()->OpenShare("sh_verify");
        store.GetVisitorManager()->StartVerify("pass123");

        QCOMPARE(ctrl.GetPageState(), QString("loading"));
    }

    void VisitorActiveSetsContentState() {
        NetworkClient nc;
        RequestFactory rf;
        SessionStore store(&nc, &rf);
        ShellController ctrl(&store);

        ctrl.navigateToVisitor("sh_active");
        store.GetVisitorManager()->OpenShare("sh_active");
        store.GetVisitorManager()->StartVerify();
        QJsonObject root_files;
        store.GetVisitorManager()->HandleVerifySuccess("st_tok", 3600, "download", root_files);

        QCOMPARE(ctrl.GetPageState(), QString("content"));
    }

    void VisitorShareExpiredClosesAndRedirectsToLogin() {
        NetworkClient nc;
        RequestFactory rf;
        SessionStore store(&nc, &rf);
        ShellController ctrl(&store);

        ctrl.navigateToVisitor("sh_expired");
        QCOMPARE(ctrl.GetCurrentShell(), QString("visitor"));

        // Simulate share expired during verify
        store.GetVisitorManager()->OpenShare("sh_expired");
        store.GetVisitorManager()->StartVerify();
        store.GetVisitorManager()->HandleVerifyFailure(60002);
        QCOMPARE(store.GetVisitorManager()->GetState(), VisitorSessionState::Idle);

        // After visitor goes Idle, falls through to owner handler
        // Owner is LoggedOut, so should redirect to login
        QCOMPARE(ctrl.GetCurrentShell(), QString("login"));
    }

    void VisitorShareExpiredWithActiveOwnerRedirectsToOwner() {
        NetworkClient nc;
        RequestFactory rf;
        SessionStore store(&nc, &rf);
        ShellController ctrl(&store);

        // Establish owner session first
        QJsonObject user;
        user["id"] = 1;
        user["username"] = "alice";
        store.GetOwnerManager()->StartLogin();
        store.GetOwnerManager()->HandleLoginSuccess("access", "refresh", 7200, user);

        ctrl.navigateToVisitor("sh_exp_owner");
        QCOMPARE(ctrl.GetCurrentShell(), QString("visitor"));

        // Share expired
        store.GetVisitorManager()->OpenShare("sh_exp_owner");
        store.GetVisitorManager()->StartVerify();
        store.GetVisitorManager()->HandleVerifyFailure(60001);
        QCOMPARE(store.GetVisitorManager()->GetState(), VisitorSessionState::Idle);

        // Falls through to owner handler; owner is Active → redirect to owner
        QCOMPARE(ctrl.GetCurrentShell(), QString("owner"));
    }

    void OwnerAuthenticatingSetsLoadingState() {
        NetworkClient nc;
        RequestFactory rf;
        SessionStore store(&nc, &rf);
        ShellController ctrl(&store);

        QCOMPARE(ctrl.GetPageState(), QString("loading"));

        ctrl.navigateToLogin();
        QCOMPARE(ctrl.GetCurrentShell(), QString("login"));

        // StartLogin triggers Authenticating state
        store.GetOwnerManager()->StartLogin();
        QCOMPARE(store.GetOwnerManager()->GetState(), OwnerSessionState::Authenticating);
        QCOMPARE(ctrl.GetPageState(), QString("loading"));
    }

    void OwnerRefreshingSetsLoadingState() {
        NetworkClient nc;
        RequestFactory rf;
        SessionStore store(&nc, &rf);
        ShellController ctrl(&store);

        QJsonObject user;
        user["id"] = 1;
        user["username"] = "alice";
        store.GetOwnerManager()->StartLogin();
        store.GetOwnerManager()->HandleLoginSuccess("access", "refresh", 7200, user);

        ctrl.Initialize();
        QCOMPARE(ctrl.GetCurrentShell(), QString("owner"));

        ctrl.setPageState("content");
        QCOMPARE(ctrl.GetPageState(), QString("content"));

        // Token expires → Refreshing state
        store.GetOwnerManager()->HandleTokenExpired();
        QCOMPARE(store.GetOwnerManager()->GetState(), OwnerSessionState::Refreshing);
        QCOMPARE(ctrl.GetPageState(), QString("loading"));
    }

    void NavigateToVisitorDoesNotAffectOwnerTokens() {
        NetworkClient nc;
        RequestFactory rf;
        SessionStore store(&nc, &rf);
        ShellController ctrl(&store);

        // Establish owner session
        QJsonObject user;
        user["id"] = 1;
        user["username"] = "alice";
        store.GetOwnerManager()->StartLogin();
        store.GetOwnerManager()->HandleLoginSuccess("access", "refresh", 7200, user);
        QCOMPARE(rf.GetOwnerAccessToken(), QString("access"));

        // Navigate to visitor
        ctrl.navigateToVisitor("sh_isolated");
        QCOMPARE(ctrl.GetCurrentShell(), QString("visitor"));

        // Owner tokens must remain intact
        QCOMPARE(rf.GetOwnerAccessToken(), QString("access"));
        QCOMPARE(store.GetOwnerManager()->GetAccessToken(), QString("access"));
        QCOMPARE(store.GetOwnerManager()->GetState(), OwnerSessionState::Active);

        // Visitor token should not be set yet (only OpenShare was called)
        QVERIFY(rf.GetVisitorShareToken().isEmpty());
    }

    void VisitorPasswordErrorStaysOnVisitor() {
        NetworkClient nc;
        RequestFactory rf;
        SessionStore store(&nc, &rf);
        ShellController ctrl(&store);

        ctrl.navigateToVisitor("sh_pwd");
        store.GetVisitorManager()->OpenShare("sh_pwd");
        store.GetVisitorManager()->StartVerify("wrong");
        store.GetVisitorManager()->HandleVerifyFailure(60003);

        // Password error → stays Unverified, shell stays visitor
        QCOMPARE(store.GetVisitorManager()->GetState(), VisitorSessionState::Unverified);
        QCOMPARE(ctrl.GetCurrentShell(), QString("visitor"));
    }

    void VisitorPasswordErrorSetsVerifyPageState() {
        NetworkClient nc;
        RequestFactory rf;
        SessionStore store(&nc, &rf);
        ShellController ctrl(&store);

        ctrl.navigateToVisitor("sh_pwd_state");
        store.GetVisitorManager()->OpenShare("sh_pwd_state");
        store.GetVisitorManager()->StartVerify("wrong");
        store.GetVisitorManager()->HandleVerifyFailure(60003);

        QCOMPARE(store.GetVisitorManager()->GetState(), VisitorSessionState::Unverified);
        QCOMPARE(ctrl.GetPageState(), QString("verify"));
    }

    void VisitorUnverifiedAfterReverifyFailureSetsVerifyState() {
        NetworkClient nc;
        RequestFactory rf;
        SessionStore store(&nc, &rf);
        ShellController ctrl(&store);

        // Full flow: visitor goes active, token expires, reverify fails
        ctrl.navigateToVisitor("sh_reverify_fail");
        store.GetVisitorManager()->OpenShare("sh_reverify_fail");
        store.GetVisitorManager()->StartVerify();
        QJsonObject root_files;
        store.GetVisitorManager()->HandleVerifySuccess("st_tok", 3600, "view", root_files);
        QCOMPARE(store.GetVisitorManager()->GetState(), VisitorSessionState::Active);
        QCOMPARE(ctrl.GetPageState(), QString("content"));

        // Token expires → ReverifyRequired → loading
        store.GetVisitorManager()->HandleTokenExpired();
        QCOMPARE(store.GetVisitorManager()->GetState(), VisitorSessionState::ReverifyRequired);
        QCOMPARE(ctrl.GetPageState(), QString("loading"));

        // Reverify fails → Unverified → verify page state
        store.GetVisitorManager()->HandleVerifyFailure(60003);
        QCOMPARE(store.GetVisitorManager()->GetState(), VisitorSessionState::Unverified);
        QCOMPARE(ctrl.GetPageState(), QString("verify"));
    }

    void VisitorFlowDoesNotContaminateOwnerShellState() {
        NetworkClient nc;
        RequestFactory rf;
        SessionStore store(&nc, &rf);
        ShellController ctrl(&store);

        // Establish owner session
        QJsonObject user;
        user["id"] = 1;
        user["username"] = "alice";
        store.GetOwnerManager()->StartLogin();
        store.GetOwnerManager()->HandleLoginSuccess("access", "refresh", 7200, user);
        ctrl.Initialize();
        QCOMPARE(ctrl.GetCurrentShell(), QString("owner"));

        // Full visitor flow with share expired
        ctrl.navigateToVisitor("sh_contaminate");
        QCOMPARE(ctrl.GetCurrentShell(), QString("visitor"));

        store.GetVisitorManager()->OpenShare("sh_contaminate");
        store.GetVisitorManager()->StartVerify();
        store.GetVisitorManager()->HandleVerifyFailure(60002);
        QCOMPARE(store.GetVisitorManager()->GetState(), VisitorSessionState::Idle);

        // Redirects back to owner (not to login), and owner tokens are still valid
        QCOMPARE(ctrl.GetCurrentShell(), QString("owner"));
        QCOMPARE(store.GetOwnerManager()->GetAccessToken(), QString("access"));
        QCOMPARE(rf.GetOwnerAccessToken(), QString("access"));
        QVERIFY(rf.GetVisitorShareToken().isEmpty());
    }
};

int run_TestShellController(int argc, char* argv[]) {
    TestShellController test;
    return QTest::qExec(&test, argc, argv);
}

#include "test_shell_controller.moc"
