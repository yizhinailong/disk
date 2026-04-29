#include <QSignalSpy>
#include <QTest>

#include "auth/SessionStore.hpp"
#include "network/NetworkClient.hpp"
#include "network/RequestFactory.hpp"

using namespace disk::desktop;

class TestSessionStore : public QObject {
    Q_OBJECT

private slots:

    void InitHasEmptyActiveDomain() {
        NetworkClient nc;
        RequestFactory rf;
        SessionStore store(&nc, &rf);

        QVERIFY(store.GetActiveDomain().isEmpty());
    }

    void InitHasOwnerAndVisitorManagers() {
        NetworkClient nc;
        RequestFactory rf;
        SessionStore store(&nc, &rf);

        QVERIFY(store.GetOwnerManager() != nullptr);
        QVERIFY(store.GetVisitorManager() != nullptr);
    }

    void ActivateOwnerSetsDomain() {
        NetworkClient nc;
        RequestFactory rf;
        SessionStore store(&nc, &rf);
        QSignalSpy spy(&store, &SessionStore::activeDomainChanged);

        store.ActivateOwner();

        QCOMPARE(store.GetActiveDomain(), QString("owner"));
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.takeFirst().at(0).toString(), QString("owner"));
    }

    void ActivateVisitorSetsDomain() {
        NetworkClient nc;
        RequestFactory rf;
        SessionStore store(&nc, &rf);
        QSignalSpy spy(&store, &SessionStore::activeDomainChanged);

        store.ActivateVisitor("sh_abc123");

        QCOMPARE(store.GetActiveDomain(), QString("visitor"));
        QCOMPARE(spy.count(), 1);
    }

    void ActivateVisitorTransitionsVisitorManager() {
        NetworkClient nc;
        RequestFactory rf;
        SessionStore store(&nc, &rf);

        store.ActivateVisitor("sh_test");

        QCOMPARE(store.GetVisitorManager()->GetState(), VisitorSessionState::Unverified);
        QCOMPARE(store.GetVisitorManager()->GetShareId(), QString("sh_test"));
    }

    void ActivateOwnerFromVisitorClosesShare() {
        NetworkClient nc;
        RequestFactory rf;
        SessionStore store(&nc, &rf);

        store.ActivateVisitor("sh_abc");
        store.GetVisitorManager()->StartVerify();
        QJsonObject root_files;
        store.GetVisitorManager()->HandleVerifySuccess("st_tok", 3600, "view", root_files);
        QCOMPARE(store.GetVisitorManager()->GetState(), VisitorSessionState::Active);

        store.ActivateOwner();

        QCOMPARE(store.GetActiveDomain(), QString("owner"));
        QCOMPARE(store.GetVisitorManager()->GetState(), VisitorSessionState::Idle);
    }

    void DeactivateAllClearsDomain() {
        NetworkClient nc;
        RequestFactory rf;
        SessionStore store(&nc, &rf);

        store.ActivateOwner();
        QCOMPARE(store.GetActiveDomain(), QString("owner"));

        QSignalSpy spy(&store, &SessionStore::activeDomainChanged);
        store.DeactivateAll();

        QVERIFY(store.GetActiveDomain().isEmpty());
        QCOMPARE(spy.count(), 1);
    }

    void DeactivateAllStartsOwnerLogout() {
        NetworkClient nc;
        RequestFactory rf;
        SessionStore store(&nc, &rf);

        QJsonObject user;
        user["id"] = 1;
        user["username"] = "alice";
        store.GetOwnerManager()->StartLogin();
        store.GetOwnerManager()->HandleLoginSuccess("access", "refresh", 7200, user);

        store.DeactivateAll();

        QCOMPARE(store.GetOwnerManager()->GetState(), OwnerSessionState::LogoutPending);
    }

    void SameDomainDoesNotEmitSignal() {
        NetworkClient nc;
        RequestFactory rf;
        SessionStore store(&nc, &rf);

        store.ActivateOwner();
        QSignalSpy spy(&store, &SessionStore::activeDomainChanged);

        store.ActivateOwner();

        QCOMPARE(spy.count(), 0);
        QCOMPARE(store.GetActiveDomain(), QString("owner"));
    }

    void ActivateOwnerThenVisitorTransitionsCorrectly() {
        NetworkClient nc;
        RequestFactory rf;
        SessionStore store(&nc, &rf);

        store.ActivateOwner();
        QCOMPARE(store.GetActiveDomain(), QString("owner"));

        store.ActivateVisitor("sh_new");
        QCOMPARE(store.GetActiveDomain(), QString("visitor"));
        QCOMPARE(store.GetVisitorManager()->GetShareId(), QString("sh_new"));
    }

    // ── Task 11: Token domain isolation regression tests ───────────────────

    void ActivateVisitorDoesNotClearOwnerTokens() {
        NetworkClient nc;
        RequestFactory rf;
        SessionStore store(&nc, &rf);

        QJsonObject user;
        user["id"] = 1;
        user["username"] = "alice";
        store.GetOwnerManager()->StartLogin();
        store.GetOwnerManager()->HandleLoginSuccess("access_tok", "refresh_tok", 7200, user);
        QCOMPARE(rf.GetOwnerAccessToken(), QString("access_tok"));

        store.ActivateVisitor("sh_isolated");
        QCOMPARE(store.GetActiveDomain(), QString("visitor"));

        QCOMPARE(rf.GetOwnerAccessToken(), QString("access_tok"));
        QCOMPARE(store.GetOwnerManager()->GetAccessToken(), QString("access_tok"));
        QCOMPARE(store.GetOwnerManager()->GetRefreshToken(), QString("refresh_tok"));
        QCOMPARE(store.GetOwnerManager()->GetState(), OwnerSessionState::Active);
    }

    void ActivateOwnerFromVisitorClearsVisitorTokens() {
        NetworkClient nc;
        RequestFactory rf;
        SessionStore store(&nc, &rf);

        store.ActivateVisitor("sh_cleanup");
        store.GetVisitorManager()->StartVerify();
        QJsonObject root_files;
        store.GetVisitorManager()->HandleVerifySuccess("st_tok", 3600, "view", root_files);
        QCOMPARE(rf.GetVisitorShareToken(), QString("st_tok"));

        store.ActivateOwner();

        QVERIFY(rf.GetVisitorShareToken().isEmpty());
        QVERIFY(store.GetVisitorManager()->GetShareToken().isEmpty());
        QCOMPARE(store.GetVisitorManager()->GetState(), VisitorSessionState::Idle);
    }

    void DeactivateAllStartsOwnerLogoutButTokensClearOnComplete() {
        NetworkClient nc;
        RequestFactory rf;
        SessionStore store(&nc, &rf);

        QJsonObject user;
        user["id"] = 1;
        user["username"] = "alice";
        store.GetOwnerManager()->StartLogin();
        store.GetOwnerManager()->HandleLoginSuccess("owner_access", "owner_refresh", 7200, user);

        store.ActivateVisitor("sh_both");
        store.GetVisitorManager()->StartVerify();
        QJsonObject root_files;
        store.GetVisitorManager()->HandleVerifySuccess("visitor_st", 3600, "download", root_files);

        QCOMPARE(rf.GetOwnerAccessToken(), QString("owner_access"));
        QCOMPARE(rf.GetVisitorShareToken(), QString("visitor_st"));

        store.DeactivateAll();

        QCOMPARE(store.GetOwnerManager()->GetState(), OwnerSessionState::LogoutPending);
        QVERIFY(rf.GetVisitorShareToken().isEmpty());

        store.GetOwnerManager()->CompleteLogout();

        QVERIFY(rf.GetOwnerAccessToken().isEmpty());
        QVERIFY(store.GetOwnerManager()->GetAccessToken().isEmpty());
    }

    void OwnerAndVisitorTokensCoexistWithoutCrossContamination() {
        NetworkClient nc;
        RequestFactory rf;
        SessionStore store(&nc, &rf);

        QJsonObject user;
        user["id"] = 1;
        user["username"] = "alice";
        store.GetOwnerManager()->StartLogin();
        store.GetOwnerManager()->HandleLoginSuccess("owner_tok", "owner_ref", 7200, user);

        store.ActivateVisitor("sh_cross");
        store.GetVisitorManager()->StartVerify();
        QJsonObject root_files;
        store.GetVisitorManager()->HandleVerifySuccess("visitor_tok", 3600, "view", root_files);

        QCOMPARE(rf.GetOwnerAccessToken(), QString("owner_tok"));
        QCOMPARE(rf.GetVisitorShareToken(), QString("visitor_tok"));

        auto owner_headers = rf.PrepareHeaders(AuthDomain::Owner);
        QVERIFY(owner_headers.contains("Authorization"));
        QVERIFY(!owner_headers.contains("X-Share-Token"));

        auto visitor_headers = rf.PrepareHeaders(AuthDomain::Visitor);
        QVERIFY(visitor_headers.contains("X-Share-Token"));
        QVERIFY(!visitor_headers.contains("Authorization"));
    }
};

int run_TestSessionStore(int argc, char* argv[]) {
    TestSessionStore test;
    return QTest::qExec(&test, argc, argv);
}

#include "test_session_store.moc"
