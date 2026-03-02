#include <QCoreApplication>
#include <QTest>

#include <storage/TokenStore.hpp>

using namespace disk::qml::storage;

class tst_TokenStore : public QObject {
    Q_OBJECT

private:
    TokenStore m_store;

private slots:

    void initTestCase() {
        QCoreApplication::setOrganizationName("DiskTest");
        QCoreApplication::setApplicationName("diskqml-test");
    }

    void init() {
        // Clear before every test
        m_store.Clear();
    }

    void cleanup() {
        // Clear after every test
        m_store.Clear();
    }

    // --- Save & Read ---

    void save_storesTokens() {
        m_store.Save(QStringLiteral("access-tok"), QStringLiteral("refresh-tok"), 7200);

        QCOMPARE(m_store.AccessToken(), QStringLiteral("access-tok"));
        QCOMPARE(m_store.RefreshToken(), QStringLiteral("refresh-tok"));
    }

    void save_updatesExpiresAt() {
        const QDateTime before = QDateTime::currentDateTimeUtc();
        m_store.Save(QStringLiteral("a"), QStringLiteral("r"), 3600);
        const QDateTime after = QDateTime::currentDateTimeUtc();

        const QDateTime expiresAt = m_store.ExpiresAt();
        QVERIFY(expiresAt.isValid());
        // expiresAt should be approximately now + 3600s
        QVERIFY(expiresAt >= before.addSecs(3600));
        QVERIFY(expiresAt <= after.addSecs(3600));
    }

    // --- Clear ---

    void clear_removesAllTokens() {
        m_store.Save(QStringLiteral("a"), QStringLiteral("r"), 3600);
        m_store.Clear();

        QVERIFY(m_store.AccessToken().isEmpty());
        QVERIFY(m_store.RefreshToken().isEmpty());
        QVERIFY(!m_store.ExpiresAt().isValid());
    }

    // --- HasValidAccessToken ---

    void hasValidAccessToken_empty_returnsFalse() {
        QVERIFY(!m_store.HasValidAccessToken());
    }

    void hasValidAccessToken_validToken_returnsTrue() {
        m_store.Save(QStringLiteral("tok"), QStringLiteral("ref"), 7200);
        QVERIFY(m_store.HasValidAccessToken());
    }

    void hasValidAccessToken_expiredToken_returnsFalse() {
        // expiresIn = 0 means expires immediately
        m_store.Save(QStringLiteral("tok"), QStringLiteral("ref"), 0);
        // With default 30s skew, this should be expired
        QVERIFY(!m_store.HasValidAccessToken());
    }

    void hasValidAccessToken_skewSeconds() {
        // Save with 60s expiry
        m_store.Save(QStringLiteral("tok"), QStringLiteral("ref"), 60);
        // With 120s skew (now + 120 > now + 60), should be "expired"
        QVERIFY(!m_store.HasValidAccessToken(120));
        // With 0s skew, should still be valid
        QVERIFY(m_store.HasValidAccessToken(0));
    }

    void hasValidAccessToken_afterClear_returnsFalse() {
        m_store.Save(QStringLiteral("tok"), QStringLiteral("ref"), 7200);
        m_store.Clear();
        QVERIFY(!m_store.HasValidAccessToken());
    }

    // --- Overwrite behavior ---

    void save_overwritesPreviousValues() {
        m_store.Save(QStringLiteral("old-a"), QStringLiteral("old-r"), 100);
        m_store.Save(QStringLiteral("new-a"), QStringLiteral("new-r"), 200);

        QCOMPARE(m_store.AccessToken(), QStringLiteral("new-a"));
        QCOMPARE(m_store.RefreshToken(), QStringLiteral("new-r"));
    }
};

QTEST_MAIN(tst_TokenStore)
#include "tst_TokenStore.moc"
