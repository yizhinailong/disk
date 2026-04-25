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
};

#include "test_shell_controller.moc"
