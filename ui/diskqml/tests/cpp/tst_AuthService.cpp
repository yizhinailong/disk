#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTest>
#include <memory>

#include <api/IAuthApi.hpp>
#include <models/AuthDtos.hpp>
#include <services/AuthService.hpp>
#include <services/TokenStore.hpp>

using namespace disk::qml;

// ---------------------------------------------------------------------------
// Fake IAuthApi — injectable callbacks per method
// ---------------------------------------------------------------------------
class FakeAuthApi : public api::IAuthApi {
public:
    std::function<void(const QString&, const QString&, const QString&, QObject*, api::AuthApiCallback)> registerFn;
    std::function<void(const QString&, const QString&, QObject*, api::AuthApiCallback)> loginFn;
    std::function<void(const QString&, QObject*, api::AuthApiCallback)> refreshFn;
    std::function<void(const QString&, QObject*, api::AuthApiCallback)> logoutFn;

    auto Register(const QString& username, const QString& email, const QString& password, QObject* ctx, api::AuthApiCallback cb) -> void override {
        if (registerFn) {
            registerFn(username, email, password, ctx, std::move(cb));
        }
    }

    auto Login(const QString& account, const QString& password, QObject* ctx, api::AuthApiCallback cb) -> void override {
        if (loginFn) {
            loginFn(account, password, ctx, std::move(cb));
        }
    }

    auto Refresh(const QString& refreshToken, QObject* ctx, api::AuthApiCallback cb) -> void override {
        if (refreshFn) {
            refreshFn(refreshToken, ctx, std::move(cb));
        }
    }

    auto Logout(const QString& accessToken, QObject* ctx, api::AuthApiCallback cb) -> void override {
        if (logoutFn) {
            logoutFn(accessToken, ctx, std::move(cb));
        }
    }
};

// ---------------------------------------------------------------------------
// Helper: build a success envelope with data payload
// ---------------------------------------------------------------------------
static auto makeEnvelope(int code, const QString& msg, const QJsonValue& data = QJsonValue{}) -> models::ApiEnvelope {
    models::ApiEnvelope env;
    env.code = code;
    env.message = msg;
    env.data = data;
    return env;
}

static auto makeLoginData() -> QJsonValue {
    QJsonObject user;
    user["id"] = 1;
    user["username"] = "testuser";
    user["email"] = "test@example.com";
    user["nickname"] = "";
    user["storage_quota"] = 10737418240.0;
    user["storage_used"] = 0;
    user["created_at"] = "2024-01-01T00:00:00Z";

    QJsonObject data;
    data["access_token"] = "at-123";
    data["refresh_token"] = "rt-456";
    data["token_type"] = "Bearer";
    data["expires_in"] = 7200;
    data["user"] = user;
    return data;
}

static auto makeRegisterData() -> QJsonValue {
    QJsonObject user;
    user["id"] = 1;
    user["username"] = "testuser";
    user["email"] = "test@example.com";
    user["nickname"] = "";
    user["storage_quota"] = 10737418240.0;
    user["storage_used"] = 0;
    user["created_at"] = "2024-01-01T00:00:00Z";

    QJsonObject data;
    data["user"] = user;
    return data;
}

// ===========================================================================
class tst_AuthService : public QObject {
    Q_OBJECT

private:
    FakeAuthApi m_fake_api;
    std::unique_ptr<QTemporaryDir> m_temp_dir;
    std::unique_ptr<services::TokenStore> m_store;
    services::AuthService* m_service = nullptr;

private slots:

    void initTestCase() {
        QCoreApplication::setOrganizationName("DiskTest");
        QCoreApplication::setApplicationName("diskqml-test");
    }

    void init() {
        m_temp_dir = std::make_unique<QTemporaryDir>();
        QVERIFY(m_temp_dir->isValid());
        m_store = std::make_unique<services::TokenStore>(m_temp_dir->path());
        m_store->Clear();
        m_fake_api.registerFn = nullptr;
        m_fake_api.loginFn = nullptr;
        m_fake_api.refreshFn = nullptr;
        m_fake_api.logoutFn = nullptr;
        m_service = new services::AuthService(&m_fake_api, m_store.get());
    }

    void cleanup() {
        delete m_service;
        m_service = nullptr;
        m_store.reset();
        m_temp_dir.reset();
    }

    // -----------------------------------------------------------------------
    // Validation tests
    // -----------------------------------------------------------------------

    void validateUsername_valid_data() {
        QTest::addColumn<QString>("input");
        QTest::addColumn<bool>("expected");

        QTest::newRow("simple") << "user1" << true;
        QTest::newRow("underscore") << "user_name" << true;
        QTest::newRow("min-length-4") << "abcd" << true;
        QTest::newRow("max-length-32") << QString(32, 'a') << true;
        QTest::newRow("digits-only") << "1234" << true;
        QTest::newRow("too-short-3") << "abc" << false;
        QTest::newRow("too-long-33") << QString(33, 'a') << false;
        QTest::newRow("space") << "ab cd" << false;
        QTest::newRow("special") << "ab!cd" << false;
        QTest::newRow("empty") << "" << false;
        QTest::newRow("chinese") << "用户名" << false;
    }

    void validateUsername_valid() {
        QFETCH(QString, input);
        QFETCH(bool, expected);
        QCOMPARE(m_service->ValidateUsername(input), expected);
    }

    void validateEmail_valid_data() {
        QTest::addColumn<QString>("input");
        QTest::addColumn<bool>("expected");

        QTest::newRow("simple") << "user@example.com" << true;
        QTest::newRow("subdomain") << "a@b.co.uk" << true;
        QTest::newRow("plus") << "u+tag@gmail.com" << true;
        QTest::newRow("dots") << "a.b@c.com" << true;
        QTest::newRow("no-at") << "userexample.com" << false;
        QTest::newRow("no-domain") << "user@" << false;
        QTest::newRow("no-tld") << "user@host" << false;
        QTest::newRow("empty") << "" << false;
        QTest::newRow("tld-1-char") << "u@h.c" << false;
    }

    void validateEmail_valid() {
        QFETCH(QString, input);
        QFETCH(bool, expected);
        QCOMPARE(m_service->ValidateEmail(input), expected);
    }

    void validatePassword_valid_data() {
        QTest::addColumn<QString>("input");
        QTest::addColumn<bool>("expected");

        QTest::newRow("basic") << "Password1" << true;
        QTest::newRow("min-8") << "Abcdefg1" << true;
        QTest::newRow("max-64") << (QString(31, 'a') + QString(31, 'A') + "12") << true;
        QTest::newRow("no-upper") << "password1" << false;
        QTest::newRow("no-lower") << "PASSWORD1" << false;
        QTest::newRow("no-digit") << "Abcdefgh" << false;
        QTest::newRow("too-short-7") << "Pass1ab" << false;
        QTest::newRow("special-char") << "Pass1ab!" << false;
        QTest::newRow("empty") << "" << false;
    }

    void validatePassword_valid() {
        QFETCH(QString, input);
        QFETCH(bool, expected);
        QCOMPARE(m_service->ValidatePassword(input), expected);
    }

    // -----------------------------------------------------------------------
    // Register tests
    // -----------------------------------------------------------------------

    void register_invalidInput_callsBackImmediately() {
        bool called = false;
        m_service->Register("ab", "bad", "bad", this, [&](auto result, QString err) {
            called = true;
            QVERIFY(!result.has_value());
            QVERIFY(!err.isEmpty());
        });
        QVERIFY(called);
    }

    void register_successEnvelope_returnsResult() {
        m_fake_api.registerFn = [](auto, auto, auto, auto, api::AuthApiCallback cb) {
            cb(makeEnvelope(0, "success", makeRegisterData()), QString{});
        };

        bool called = false;
        m_service->Register("testuser", "test@example.com", "Password1", this, [&](auto result, QString err) {
            called = true;
            QVERIFY(result.has_value());
            QVERIFY(err.isEmpty());
            QCOMPARE(result->user.username, QStringLiteral("testuser"));
        });
        QVERIFY(called);
    }

    void register_errorEnvelope_returnsError() {
        m_fake_api.registerFn = [](auto, auto, auto, auto, api::AuthApiCallback cb) {
            cb(makeEnvelope(40001, "username exists"), QString{});
        };

        bool called = false;
        m_service->Register("testuser", "test@example.com", "Password1", this, [&](auto result, QString err) {
            called = true;
            QVERIFY(!result.has_value());
            QVERIFY(!err.isEmpty());
        });
        QVERIFY(called);
    }

    void register_networkError_returnsError() {
        m_fake_api.registerFn = [](auto, auto, auto, auto, api::AuthApiCallback cb) {
            cb(models::ApiEnvelope{}, QStringLiteral("connection refused"));
        };

        bool called = false;
        m_service->Register("testuser", "test@example.com", "Password1", this, [&](auto result, QString err) {
            called = true;
            QVERIFY(!result.has_value());
            QVERIFY(!err.isEmpty());
        });
        QVERIFY(called);
    }

    void register_malformedData_returnsParseError() {
        m_fake_api.registerFn = [](auto, auto, auto, auto, api::AuthApiCallback cb) {
            // Success code but empty data
            cb(makeEnvelope(0, "success", QJsonValue{}), QString{});
        };

        bool called = false;
        m_service->Register("testuser", "test@example.com", "Password1", this, [&](auto result, QString err) {
            called = true;
            QVERIFY(!result.has_value());
            QVERIFY(!err.isEmpty());
        });
        QVERIFY(called);
    }

    // -----------------------------------------------------------------------
    // Login tests
    // -----------------------------------------------------------------------

    void login_emptyAccount_callsBackImmediately() {
        bool called = false;
        m_service->Login("  ", "Password1", this, [&](auto result, QString err) {
            called = true;
            QVERIFY(!result.has_value());
            QVERIFY(!err.isEmpty());
        });
        QVERIFY(called);
    }

    void login_emptyPassword_callsBackImmediately() {
        bool called = false;
        m_service->Login("testuser", "", this, [&](auto result, QString err) {
            called = true;
            QVERIFY(!result.has_value());
            QVERIFY(!err.isEmpty());
        });
        QVERIFY(called);
    }

    void login_success_storesTokens() {
        m_fake_api.loginFn = [](auto, auto, auto, api::AuthApiCallback cb) {
            cb(makeEnvelope(0, "success", makeLoginData()), QString{});
        };

        bool called = false;
        m_service->Login("testuser", "Password1", this, [&](auto result, QString err) {
            called = true;
            QVERIFY(result.has_value());
            QVERIFY(err.isEmpty());
            QCOMPARE(result->accessToken, QStringLiteral("at-123"));
        });
        QVERIFY(called);

        // Verify tokens persisted in store
        QCOMPARE(m_store->AccessToken(), QStringLiteral("at-123"));
        QCOMPARE(m_store->RefreshToken(), QStringLiteral("rt-456"));
        QVERIFY(m_store->HasValidAccessToken());
    }

    void login_errorEnvelope_doesNotStoreTokens() {
        m_fake_api.loginFn = [](auto, auto, auto, api::AuthApiCallback cb) {
            cb(makeEnvelope(40101, "invalid credentials"), QString{});
        };

        m_service->Login("testuser", "Password1", this, [&](auto result, QString err) {
            QVERIFY(!result.has_value());
            QVERIFY(!err.isEmpty());
        });

        QVERIFY(m_store->AccessToken().isEmpty());
    }

    void login_networkError_doesNotStoreTokens() {
        m_fake_api.loginFn = [](auto, auto, auto, api::AuthApiCallback cb) {
            cb(models::ApiEnvelope{}, QStringLiteral("timeout"));
        };

        m_service->Login("testuser", "Password1", this, [&](auto result, QString err) {
            QVERIFY(!result.has_value());
            QVERIFY(!err.isEmpty());
        });

        QVERIFY(m_store->AccessToken().isEmpty());
    }

    // -----------------------------------------------------------------------
    // Refresh tests
    // -----------------------------------------------------------------------

    void refresh_emptyToken_callsBackImmediately() {
        bool called = false;
        m_service->Refresh("  ", this, [&](auto result, QString err) {
            called = true;
            QVERIFY(!result.has_value());
            QVERIFY(!err.isEmpty());
        });
        QVERIFY(called);
    }

    void refresh_success_updatesTokenStore() {
        QJsonObject refreshData;
        refreshData["access_token"] = "new-at";
        refreshData["refresh_token"] = "new-rt";
        refreshData["expires_in"] = 3600;

        m_fake_api.refreshFn = [&](auto, auto, api::AuthApiCallback cb) {
            cb(makeEnvelope(0, "success", QJsonValue(refreshData)), QString{});
        };

        bool called = false;
        m_service->Refresh("old-rt", this, [&](auto result, QString err) {
            called = true;
            QVERIFY(result.has_value());
            QVERIFY(err.isEmpty());
        });
        QVERIFY(called);

        QCOMPARE(m_store->AccessToken(), QStringLiteral("new-at"));
        QCOMPARE(m_store->RefreshToken(), QStringLiteral("new-rt"));
    }

    // -----------------------------------------------------------------------
    // Logout tests
    // -----------------------------------------------------------------------

    void logout_emptyToken_clearsStoreAndSucceeds() {
        m_store->Save("a", "r", 3600);

        bool called = false;
        m_service->Logout("", this, [&](bool ok, QString err) {
            called = true;
            QVERIFY(ok);
            QVERIFY(err.isEmpty());
        });
        QVERIFY(called);
        QVERIFY(m_store->AccessToken().isEmpty());
    }

    void logout_successEnvelope_clearsStore() {
        m_store->Save("a", "r", 3600);

        m_fake_api.logoutFn = [](auto, auto, api::AuthApiCallback cb) {
            cb(makeEnvelope(0, "success"), QString{});
        };

        bool called = false;
        m_service->Logout("a", this, [&](bool ok, QString err) {
            called = true;
            QVERIFY(ok);
            QVERIFY(err.isEmpty());
        });
        QVERIFY(called);
        QVERIFY(m_store->AccessToken().isEmpty());
    }

    void logout_tokenExpiredCode_stillClearsStore() {
        m_store->Save("a", "r", 3600);

        m_fake_api.logoutFn = [](auto, auto, api::AuthApiCallback cb) {
            // 40108 = TokenExpired — should still be treated as success
            cb(makeEnvelope(40108, "token expired"), QString{});
        };

        bool called = false;
        m_service->Logout("a", this, [&](bool ok, QString err) {
            called = true;
            QVERIFY(ok);
            QVERIFY(err.isEmpty());
        });
        QVERIFY(called);
        QVERIFY(m_store->AccessToken().isEmpty());
    }

    void logout_networkError_doesNotClearStore() {
        m_store->Save("a", "r", 3600);

        m_fake_api.logoutFn = [](auto, auto, api::AuthApiCallback cb) {
            cb(models::ApiEnvelope{}, QStringLiteral("timeout"));
        };

        m_service->Logout("a", this, [&](bool ok, QString err) {
            QVERIFY(!ok);
            QVERIFY(!err.isEmpty());
        });

        // Store should NOT be cleared on network error
        QCOMPARE(m_store->AccessToken(), QStringLiteral("a"));
    }

    void logout_serverError_doesNotClearStore() {
        m_store->Save("a", "r", 3600);

        m_fake_api.logoutFn = [](auto, auto, api::AuthApiCallback cb) {
            cb(makeEnvelope(10006, "internal error"), QString{});
        };

        m_service->Logout("a", this, [&](bool ok, QString err) {
            QVERIFY(!ok);
            QVERIFY(!err.isEmpty());
        });

        QCOMPARE(m_store->AccessToken(), QStringLiteral("a"));
    }
};

QTEST_MAIN(tst_AuthService)
#include "tst_AuthService.moc"
