#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QTemporaryDir>
#include <QTest>

#include <storage/TokenStore.hpp>

using namespace disk::qml::storage;

class tst_TokenStore : public QObject {
    Q_OBJECT

private:
    std::unique_ptr<QTemporaryDir> m_temp_dir;
    std::unique_ptr<TokenStore> m_store;

    auto storePath() const -> QString { return m_temp_dir->path(); }

    auto tokenFilePath() const -> QString { return storePath() + QStringLiteral("/token.json"); }

private slots:

    void initTestCase() {
        QCoreApplication::setOrganizationName("DiskTest");
        QCoreApplication::setApplicationName("diskqml-test");
    }

    void init() {
        m_temp_dir = std::make_unique<QTemporaryDir>();
        QVERIFY(m_temp_dir->isValid());
        m_store = std::make_unique<TokenStore>(storePath());
        m_store->Clear();
    }

    void cleanup() {
        m_store->Clear();
        m_store.reset();
        m_temp_dir.reset();
    }

    // --- Save & Read ---

    void save_storesTokens() {
        m_store->Save(QStringLiteral("access-tok"), QStringLiteral("refresh-tok"), 7200);

        QCOMPARE(m_store->AccessToken(), QStringLiteral("access-tok"));
        QCOMPARE(m_store->RefreshToken(), QStringLiteral("refresh-tok"));
    }

    void save_updatesExpiresAt() {
        const QDateTime before = QDateTime::currentDateTimeUtc();
        m_store->Save(QStringLiteral("a"), QStringLiteral("r"), 3600);
        const QDateTime after = QDateTime::currentDateTimeUtc();

        const QDateTime expiresAt = m_store->ExpiresAt();
        QVERIFY(expiresAt.isValid());
        // expiresAt should be approximately now + 3600s
        QVERIFY(expiresAt >= before.addSecs(3600));
        QVERIFY(expiresAt <= after.addSecs(3600));
    }

    // --- Clear ---

    void clear_removesAllTokens() {
        m_store->Save(QStringLiteral("a"), QStringLiteral("r"), 3600);
        m_store->Clear();

        QVERIFY(m_store->AccessToken().isEmpty());
        QVERIFY(m_store->RefreshToken().isEmpty());
        QVERIFY(!m_store->ExpiresAt().isValid());
    }

    // --- HasValidAccessToken ---

    void hasValidAccessToken_empty_returnsFalse() {
        QVERIFY(!m_store->HasValidAccessToken());
    }

    void hasValidAccessToken_validToken_returnsTrue() {
        m_store->Save(QStringLiteral("tok"), QStringLiteral("ref"), 7200);
        QVERIFY(m_store->HasValidAccessToken());
    }

    void hasValidAccessToken_expiredToken_returnsFalse() {
        // expiresIn = 0 means expires immediately
        m_store->Save(QStringLiteral("tok"), QStringLiteral("ref"), 0);
        // With default 30s skew, this should be expired
        QVERIFY(!m_store->HasValidAccessToken());
    }

    void hasValidAccessToken_skewSeconds() {
        // Save with 60s expiry
        m_store->Save(QStringLiteral("tok"), QStringLiteral("ref"), 60);
        // With 120s skew (now + 120 > now + 60), should be "expired"
        QVERIFY(!m_store->HasValidAccessToken(120));
        // With 0s skew, should still be valid
        QVERIFY(m_store->HasValidAccessToken(0));
    }

    void hasValidAccessToken_afterClear_returnsFalse() {
        m_store->Save(QStringLiteral("tok"), QStringLiteral("ref"), 7200);
        m_store->Clear();
        QVERIFY(!m_store->HasValidAccessToken());
    }

    // --- Overwrite behavior ---

    void save_overwritesPreviousValues() {
        m_store->Save(QStringLiteral("old-a"), QStringLiteral("old-r"), 100);
        m_store->Save(QStringLiteral("new-a"), QStringLiteral("new-r"), 200);

        QCOMPARE(m_store->AccessToken(), QStringLiteral("new-a"));
        QCOMPARE(m_store->RefreshToken(), QStringLiteral("new-r"));
    }

    // --- New: File-based persistence tests ---

    void save_createsJsonFile() {
        QVERIFY(!QFile::exists(tokenFilePath()));

        m_store->Save(QStringLiteral("a"), QStringLiteral("r"), 3600);

        QVERIFY(QFile::exists(tokenFilePath()));

        // Verify JSON content
        QFile file(tokenFilePath());
        QVERIFY(file.open(QIODevice::ReadOnly));
        const auto doc = QJsonDocument::fromJson(file.readAll());
        file.close();
        QVERIFY(doc.isObject());
        const auto obj = doc.object();
        QCOMPARE(obj.value("version").toInt(), 1);
        QCOMPARE(obj.value("accessToken").toString(), QStringLiteral("a"));
        QCOMPARE(obj.value("refreshToken").toString(), QStringLiteral("r"));
        QVERIFY(obj.contains("expiresAtEpochMs"));
    }

    void corruptedFile_treatedAsLoggedOut_andDeletesFile() {
        // Write garbage to token.json
        QFile file(tokenFilePath());
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("NOT VALID JSON {{{");
        file.close();
        QVERIFY(QFile::exists(tokenFilePath()));

        // Reading should return empty (logged-out)
        QVERIFY(m_store->AccessToken().isEmpty());

        // Corrupted file should have been deleted
        QVERIFY(!QFile::exists(tokenFilePath()));
    }

    void save_createsMissingDirectory() {
        // Construct with a non-existent subdirectory
        const QString subDir = storePath() + QStringLiteral("/deep/nested/dir");
        QVERIFY(!QDir(subDir).exists());

        TokenStore nestedStore(subDir);
        nestedStore.Save(QStringLiteral("a"), QStringLiteral("r"), 3600);

        QVERIFY(QDir(subDir).exists());
        QVERIFY(QFile::exists(subDir + QStringLiteral("/token.json")));
        QCOMPARE(nestedStore.AccessToken(), QStringLiteral("a"));
    }

    void migration_importsLegacyQSettings_thenClearsLegacyKeys() {
        // Write legacy data to QSettings
        {
            QSettings settings;
            settings.beginGroup(QStringLiteral("auth"));
            settings.setValue(QStringLiteral("accessToken"), QStringLiteral("legacy-access"));
            settings.setValue(QStringLiteral("refreshToken"), QStringLiteral("legacy-refresh"));
            settings.setValue(QStringLiteral("expiresAt"), QDateTime::currentDateTimeUtc().addSecs(7200));
            settings.endGroup();
            settings.sync();
        }

        // Create a NEW temp dir (no token.json exists yet) to trigger migration
        QTemporaryDir migrationDir;
        QVERIFY(migrationDir.isValid());

        TokenStore migratedStore(migrationDir.path());

        // Should have migrated values
        QCOMPARE(migratedStore.AccessToken(), QStringLiteral("legacy-access"));
        QCOMPARE(migratedStore.RefreshToken(), QStringLiteral("legacy-refresh"));
        QVERIFY(migratedStore.ExpiresAt().isValid());
        QVERIFY(migratedStore.HasValidAccessToken());

        // Legacy QSettings keys should be cleared
        {
            QSettings settings;
            settings.beginGroup(QStringLiteral("auth"));
            QVERIFY(settings.value(QStringLiteral("accessToken")).toString().isEmpty());
            QVERIFY(settings.value(QStringLiteral("refreshToken")).toString().isEmpty());
            settings.endGroup();
        }
    }
};

QTEST_MAIN(tst_TokenStore)
#include "tst_TokenStore.moc"
