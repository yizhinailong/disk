#include <QJsonArray>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTest>

#include "auth/VisitorSessionManager.hpp"
#include "network/NetworkClient.hpp"
#include "network/RequestFactory.hpp"

using namespace disk::desktop;

class TestVisitorSession : public QObject {
    Q_OBJECT

private slots:

    void InitStateIsIdle() {
        NetworkClient nc;
        RequestFactory rf;
        VisitorSessionManager mgr(&nc, &rf);

        QCOMPARE(mgr.GetState(), VisitorSessionState::Idle);
        QVERIFY(mgr.GetShareId().isEmpty());
        QVERIFY(mgr.GetShareToken().isEmpty());
        QVERIFY(mgr.GetPermission().isEmpty());
    }

    void OpenShareTransition() {
        NetworkClient nc;
        RequestFactory rf;
        VisitorSessionManager mgr(&nc, &rf);
        QSignalSpy id_spy(&mgr, &VisitorSessionManager::shareIdChanged);

        mgr.OpenShare("sh_abc123");

        QCOMPARE(mgr.GetState(), VisitorSessionState::Unverified);
        QCOMPARE(mgr.GetShareId(), QString("sh_abc123"));
        QCOMPARE(id_spy.count(), 1);
        QCOMPARE(id_spy.takeFirst().at(0).toString(), QString("sh_abc123"));
    }

    void VerifyTransition() {
        NetworkClient nc;
        RequestFactory rf;
        VisitorSessionManager mgr(&nc, &rf);
        QSignalSpy verify_spy(&mgr, &VisitorSessionManager::verifyRequested);

        mgr.OpenShare("sh_abc");
        mgr.StartVerify("pass123");

        QCOMPARE(mgr.GetState(), VisitorSessionState::Verifying);
        QCOMPARE(verify_spy.count(), 1);
        auto args = verify_spy.takeFirst();
        QCOMPARE(args.at(0).toString(), QString("sh_abc"));
        QCOMPARE(args.at(1).toString(), QString("pass123"));
    }

    void VerifySuccessTransition() {
        NetworkClient nc;
        RequestFactory rf;
        VisitorSessionManager mgr(&nc, &rf);
        QSignalSpy established_spy(&mgr, &VisitorSessionManager::sessionEstablished);

        mgr.OpenShare("sh_abc");
        mgr.StartVerify();

        QJsonObject root_files;
        root_files["items"] = QJsonArray();
        mgr.HandleVerifySuccess("st_token123", 3600, "download", root_files);

        QCOMPARE(mgr.GetState(), VisitorSessionState::Active);
        QCOMPARE(mgr.GetShareToken(), QString("st_token123"));
        QCOMPARE(mgr.GetPermission(), QString("download"));
        QCOMPARE(established_spy.count(), 1);
        QCOMPARE(established_spy.takeFirst().at(0).toString(), QString("download"));
    }

    void VerifyPasswordErrorStaysUnverified() {
        NetworkClient nc;
        RequestFactory rf;
        VisitorSessionManager mgr(&nc, &rf);

        mgr.OpenShare("sh_abc");
        mgr.StartVerify("wrong");

        mgr.HandleVerifyFailure(60003);
        QCOMPARE(mgr.GetState(), VisitorSessionState::Unverified);
    }

    void VerifyShareNotFoundCloses() {
        NetworkClient nc;
        RequestFactory rf;
        VisitorSessionManager mgr(&nc, &rf);
        QSignalSpy closed_spy(&mgr, &VisitorSessionManager::shareClosed);

        mgr.OpenShare("sh_abc");
        mgr.StartVerify();

        mgr.HandleVerifyFailure(60001);
        QCOMPARE(mgr.GetState(), VisitorSessionState::Idle);
        QCOMPARE(closed_spy.count(), 1);
    }

    void VerifyShareExpiredCloses() {
        NetworkClient nc;
        RequestFactory rf;
        VisitorSessionManager mgr(&nc, &rf);

        mgr.OpenShare("sh_abc");
        mgr.StartVerify();

        mgr.HandleVerifyFailure(60002);
        QCOMPARE(mgr.GetState(), VisitorSessionState::Idle);
    }

    void TokenExpiredTransitionsToReverify() {
        NetworkClient nc;
        RequestFactory rf;
        VisitorSessionManager mgr(&nc, &rf);
        QSignalSpy reverify_spy(&mgr, &VisitorSessionManager::reverifyRequested);

        mgr.OpenShare("sh_abc");
        mgr.StartVerify();
        QJsonObject root_files;
        mgr.HandleVerifySuccess("st_token", 3600, "view", root_files);
        QCOMPARE(mgr.GetState(), VisitorSessionState::Active);

        mgr.HandleTokenExpired();
        QCOMPARE(mgr.GetState(), VisitorSessionState::ReverifyRequired);
        QVERIFY(mgr.GetShareToken().isEmpty());
        QCOMPARE(reverify_spy.count(), 1);
    }

    void CloseShareTransitionsToIdle() {
        NetworkClient nc;
        RequestFactory rf;
        VisitorSessionManager mgr(&nc, &rf);
        QSignalSpy closed_spy(&mgr, &VisitorSessionManager::shareClosed);

        mgr.OpenShare("sh_abc");
        mgr.StartVerify();
        QJsonObject root_files;
        mgr.HandleVerifySuccess("st_token", 3600, "download", root_files);
        QCOMPARE(mgr.GetState(), VisitorSessionState::Active);

        mgr.CloseShare();
        QCOMPARE(mgr.GetState(), VisitorSessionState::Idle);
        QVERIFY(mgr.GetShareId().isEmpty());
        QVERIFY(mgr.GetShareToken().isEmpty());
        QVERIFY(mgr.GetPermission().isEmpty());
        QCOMPARE(closed_spy.count(), 1);
    }

    void NoRefreshMechanism() {
        NetworkClient nc;
        RequestFactory rf;
        VisitorSessionManager mgr(&nc, &rf);

        QVERIFY(mgr.metaObject()->indexOfSignal("refreshRequested(QString)") == -1);
    }
};

#include "test_visitor_session.moc"
