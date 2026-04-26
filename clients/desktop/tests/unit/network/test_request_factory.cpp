#include <QTest>

#include "network/RequestFactory.hpp"

using namespace disk::desktop;

class TestRequestFactory : public QObject {
    Q_OBJECT

private slots:

    void PublicDomainReturnsEmptyHeaders() {
        RequestFactory rf;
        auto headers = rf.PrepareHeaders(AuthDomain::Public);
        QVERIFY(headers.isEmpty());
    }

    void OwnerDomainWithTokenReturnsBearer() {
        RequestFactory rf;
        rf.SetOwnerAccessToken("test_access_123");
        auto headers = rf.PrepareHeaders(AuthDomain::Owner);

        QCOMPARE(headers.size(), 1);
        QCOMPARE(headers["Authorization"], QString("Bearer test_access_123"));
    }

    void OwnerDomainWithoutTokenReturnsEmpty() {
        RequestFactory rf;
        auto headers = rf.PrepareHeaders(AuthDomain::Owner);
        QVERIFY(headers.isEmpty());
    }

    void VisitorDomainWithTokenReturnsShareToken() {
        RequestFactory rf;
        rf.SetVisitorShareToken("st_share_456");
        auto headers = rf.PrepareHeaders(AuthDomain::Visitor);

        QCOMPARE(headers.size(), 1);
        QCOMPARE(headers["X-Share-Token"], QString("st_share_456"));
    }

    void VisitorDomainWithoutTokenReturnsEmpty() {
        RequestFactory rf;
        auto headers = rf.PrepareHeaders(AuthDomain::Visitor);
        QVERIFY(headers.isEmpty());
    }

    void OwnerDoesNotIncludeShareToken() {
        RequestFactory rf;
        rf.SetOwnerAccessToken("access_tok");
        rf.SetVisitorShareToken("share_tok");
        auto headers = rf.PrepareHeaders(AuthDomain::Owner);

        QVERIFY(!headers.contains("X-Share-Token"));
        QCOMPARE(headers["Authorization"], QString("Bearer access_tok"));
    }

    void VisitorDoesNotIncludeBearer() {
        RequestFactory rf;
        rf.SetOwnerAccessToken("access_tok");
        rf.SetVisitorShareToken("share_tok");
        auto headers = rf.PrepareHeaders(AuthDomain::Visitor);

        QVERIFY(!headers.contains("Authorization"));
        QCOMPARE(headers["X-Share-Token"], QString("share_tok"));
    }

    void ClearOwnerToken() {
        RequestFactory rf;
        rf.SetOwnerAccessToken("access");
        QCOMPARE(rf.GetOwnerAccessToken(), QString("access"));

        rf.ClearOwnerToken();
        QVERIFY(rf.GetOwnerAccessToken().isEmpty());

        auto headers = rf.PrepareHeaders(AuthDomain::Owner);
        QVERIFY(headers.isEmpty());
    }

    void ClearVisitorToken() {
        RequestFactory rf;
        rf.SetVisitorShareToken("share");
        QCOMPARE(rf.GetVisitorShareToken(), QString("share"));

        rf.ClearVisitorToken();
        QVERIFY(rf.GetVisitorShareToken().isEmpty());

        auto headers = rf.PrepareHeaders(AuthDomain::Visitor);
        QVERIFY(headers.isEmpty());
    }

    void TokensAreIndependent() {
        RequestFactory rf;
        rf.SetOwnerAccessToken("owner_tok");
        rf.SetVisitorShareToken("visitor_tok");

        QCOMPARE(rf.GetOwnerAccessToken(), QString("owner_tok"));
        QCOMPARE(rf.GetVisitorShareToken(), QString("visitor_tok"));

        rf.ClearOwnerToken();
        QCOMPARE(rf.GetVisitorShareToken(), QString("visitor_tok"));

        rf.SetOwnerAccessToken("new_owner");
        QCOMPARE(rf.GetOwnerAccessToken(), QString("new_owner"));
    }
};

int run_TestRequestFactory(int argc, char* argv[]) {
    TestRequestFactory test;
    return QTest::qExec(&test, argc, argv);
}

#include "test_request_factory.moc"
