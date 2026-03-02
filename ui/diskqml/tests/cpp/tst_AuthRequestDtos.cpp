#include <QJsonObject>
#include <QTest>

#include <dtos/AuthDtos.hpp>

using namespace disk::qml::models;

class tst_AuthRequestDtos : public QObject {
    Q_OBJECT

private slots:

    // --- RegisterRequest::ToJsonObject ---

    void registerRequest_toJsonObject_hasThreeKeys() {
        RegisterRequest req;
        req.username = QStringLiteral("testuser");
        req.email = QStringLiteral("test@example.com");
        req.password = QStringLiteral("secret123");

        const QJsonObject obj = req.ToJsonObject();
        QCOMPARE(obj.size(), 3);
    }

    void registerRequest_toJsonObject_correctValues() {
        RegisterRequest req;
        req.username = QStringLiteral("testuser");
        req.email = QStringLiteral("test@example.com");
        req.password = QStringLiteral("secret123");

        const QJsonObject obj = req.ToJsonObject();
        QCOMPARE(obj.value(QLatin1String("username")).toString(), QStringLiteral("testuser"));
        QCOMPARE(obj.value(QLatin1String("email")).toString(), QStringLiteral("test@example.com"));
        QCOMPARE(obj.value(QLatin1String("password")).toString(), QStringLiteral("secret123"));
    }

    void registerRequest_emptyStrings_keysStillPresent() {
        RegisterRequest req;
        // username, email, password all default-constructed as empty strings

        const QJsonObject obj = req.ToJsonObject();
        QCOMPARE(obj.size(), 3);
        QVERIFY(obj.contains(QLatin1String("username")));
        QVERIFY(obj.contains(QLatin1String("email")));
        QVERIFY(obj.contains(QLatin1String("password")));
        QCOMPARE(obj.value(QLatin1String("username")).toString(), QStringLiteral(""));
        QCOMPARE(obj.value(QLatin1String("email")).toString(), QStringLiteral(""));
        QCOMPARE(obj.value(QLatin1String("password")).toString(), QStringLiteral(""));
    }

    // --- LoginRequest::ToJsonObject ---

    void loginRequest_toJsonObject_hasTwoKeys() {
        LoginRequest req;
        req.account = QStringLiteral("testuser");
        req.password = QStringLiteral("secret123");

        const QJsonObject obj = req.ToJsonObject();
        QCOMPARE(obj.size(), 2);
    }

    void loginRequest_toJsonObject_correctValues() {
        LoginRequest req;
        req.account = QStringLiteral("testuser");
        req.password = QStringLiteral("secret123");

        const QJsonObject obj = req.ToJsonObject();
        QCOMPARE(obj.value(QLatin1String("account")).toString(), QStringLiteral("testuser"));
        QCOMPARE(obj.value(QLatin1String("password")).toString(), QStringLiteral("secret123"));
    }

    void loginRequest_emptyStrings_keysStillPresent() {
        LoginRequest req;
        // account, password all default-constructed as empty strings

        const QJsonObject obj = req.ToJsonObject();
        QCOMPARE(obj.size(), 2);
        QVERIFY(obj.contains(QLatin1String("account")));
        QVERIFY(obj.contains(QLatin1String("password")));
        QCOMPARE(obj.value(QLatin1String("account")).toString(), QStringLiteral(""));
        QCOMPARE(obj.value(QLatin1String("password")).toString(), QStringLiteral(""));
    }

    // --- RefreshTokenRequest::ToJsonObject ---

    void refreshTokenRequest_toJsonObject_hasRefreshTokenKey() {
        RefreshTokenRequest req;
        req.refreshToken = QStringLiteral("my-refresh-token");

        const QJsonObject obj = req.ToJsonObject();
        QVERIFY(obj.contains(QLatin1String("refresh_token")));
        QCOMPARE(obj.value(QLatin1String("refresh_token")).toString(), QStringLiteral("my-refresh-token"));
    }

    void refreshTokenRequest_toJsonObject_correctKeyName() {
        // Verify exact key name is "refresh_token" (not "refreshToken" or other)
        RefreshTokenRequest req;
        req.refreshToken = QStringLiteral("token-value");

        const QJsonObject obj = req.ToJsonObject();
        QCOMPARE(obj.size(), 1);
        QVERIFY(obj.contains(QLatin1String("refresh_token")));
        QVERIFY(!obj.contains(QLatin1String("refreshToken")));
    }

    void refreshTokenRequest_emptyString_keyStillPresent() {
        RefreshTokenRequest req;
        // refreshToken default-constructed as empty string

        const QJsonObject obj = req.ToJsonObject();
        QCOMPARE(obj.size(), 1);
        QVERIFY(obj.contains(QLatin1String("refresh_token")));
        QCOMPARE(obj.value(QLatin1String("refresh_token")).toString(), QStringLiteral(""));
    }
};

QTEST_APPLESS_MAIN(tst_AuthRequestDtos)
#include "tst_AuthRequestDtos.moc"
