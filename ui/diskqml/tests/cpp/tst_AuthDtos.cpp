#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QTest>

#include <models/AuthDtos.hpp>

using namespace disk::qml::models;

class tst_AuthDtos : public QObject {
    Q_OBJECT

private:
    static auto makeDoc(const QByteArray& json) -> QJsonDocument {
        return QJsonDocument::fromJson(json);
    }

private slots:

    // --- ParseEnvelope ---

    void parseEnvelope_validJson() {
        const auto doc = makeDoc(R"({"code": 0, "message": "success", "data": {"foo": "bar"}})");
        auto env = ParseEnvelope(doc);
        QVERIFY(env.has_value());
        QCOMPARE(env->code, 0);
        QCOMPARE(env->message, QStringLiteral("success"));
        QVERIFY(env->data.isObject());
    }

    void parseEnvelope_missingCode() {
        const auto doc = makeDoc(R"({"message": "oops"})");
        QVERIFY(!ParseEnvelope(doc).has_value());
    }

    void parseEnvelope_missingMessage() {
        const auto doc = makeDoc(R"({"code": 0})");
        QVERIFY(!ParseEnvelope(doc).has_value());
    }

    void parseEnvelope_notAnObject() {
        const auto doc = makeDoc(R"([1, 2, 3])");
        QVERIFY(!ParseEnvelope(doc).has_value());
    }

    void parseEnvelope_nullData() {
        const auto doc = makeDoc(R"({"code": 0, "message": "ok", "data": null})");
        auto env = ParseEnvelope(doc);
        QVERIFY(env.has_value());
        QVERIFY(env->data.isNull());
    }

    void parseEnvelope_noDataField() {
        const auto doc = makeDoc(R"({"code": 0, "message": "ok"})");
        auto env = ParseEnvelope(doc);
        QVERIFY(env.has_value());
        // QJsonObject::value() for missing key returns QJsonValue::Undefined
        QVERIFY(env->data.isUndefined());
    }

    void parseEnvelope_errorCode() {
        const auto doc = makeDoc(R"({"code": 40001, "message": "username exists"})");
        auto env = ParseEnvelope(doc);
        QVERIFY(env.has_value());
        QCOMPARE(env->code, 40001);
        QCOMPARE(env->message, QStringLiteral("username exists"));
    }

    // --- ParseUserDto ---

    void parseUserDto_valid() {
        QJsonObject obj;
        obj["id"] = 1;
        obj["username"] = "testuser";
        obj["email"] = "test@example.com";
        obj["nickname"] = "";
        obj["storage_quota"] = 10737418240.0;
        obj["storage_used"] = 0;
        obj["created_at"] = "2024-01-01T00:00:00Z";

        auto user = ParseUserDto(obj);
        QVERIFY(user.has_value());
        QCOMPARE(user->id, quint64(1));
        QCOMPARE(user->username, QStringLiteral("testuser"));
        QCOMPARE(user->email, QStringLiteral("test@example.com"));
        QCOMPARE(user->nickname, QStringLiteral(""));
        QCOMPARE(user->storageQuota, quint64(10737418240));
        QCOMPARE(user->storageUsed, quint64(0));
        QCOMPARE(user->createdAt, QStringLiteral("2024-01-01T00:00:00Z"));
    }

    void parseUserDto_emptyUsername_returnsNullopt() {
        QJsonObject obj;
        obj["id"] = 1;
        obj["username"] = "";
        obj["email"] = "test@example.com";

        QVERIFY(!ParseUserDto(obj).has_value());
    }

    void parseUserDto_emptyObject_returnsNullopt() {
        QVERIFY(!ParseUserDto(QJsonObject{}).has_value());
    }

    // --- ParseRegisterResult ---

    void parseRegisterResult_valid() {
        const auto doc = makeDoc(R"({
            "user": {
                "id": 1,
                "username": "testuser",
                "email": "test@example.com",
                "nickname": "",
                "storage_quota": 10737418240,
                "storage_used": 0,
                "created_at": "2024-01-01T00:00:00Z"
            }
        })");

        auto result = ParseRegisterResult(QJsonValue(doc.object()));
        QVERIFY(result.has_value());
        QCOMPARE(result->user.username, QStringLiteral("testuser"));
        QCOMPARE(result->user.email, QStringLiteral("test@example.com"));
    }

    void parseRegisterResult_fullEnvelope() {
        // Test with a full envelope like the sample JSON from the task
        const auto doc = makeDoc(R"({
            "code": 0,
            "message": "success",
            "data": {
                "user": {
                    "id": 1,
                    "username": "testuser",
                    "email": "test@example.com",
                    "nickname": "",
                    "storage_quota": 10737418240,
                    "storage_used": 0,
                    "created_at": "2024-01-01T00:00:00Z"
                }
            }
        })");

        auto env = ParseEnvelope(doc);
        QVERIFY(env.has_value());
        QCOMPARE(env->code, 0);

        auto result = ParseRegisterResult(env->data);
        QVERIFY(result.has_value());
        QCOMPARE(result->user.id, quint64(1));
        QCOMPARE(result->user.username, QStringLiteral("testuser"));
    }

    void parseRegisterResult_missingUser_returnsNullopt() {
        const auto doc = makeDoc(R"({"other": 1})");
        QVERIFY(!ParseRegisterResult(QJsonValue(doc.object())).has_value());
    }

    void parseRegisterResult_notObject_returnsNullopt() {
        QVERIFY(!ParseRegisterResult(QJsonValue(42)).has_value());
        QVERIFY(!ParseRegisterResult(QJsonValue(QJsonValue::Null)).has_value());
    }

    // --- ParseLoginResult ---

    void parseLoginResult_valid() {
        const auto doc = makeDoc(R"({
            "access_token": "jwt-access-token",
            "refresh_token": "jwt-refresh-token",
            "token_type": "Bearer",
            "expires_in": 7200,
            "user": {
                "id": 1,
                "username": "testuser",
                "email": "test@example.com",
                "nickname": "",
                "storage_quota": 10737418240,
                "storage_used": 0,
                "created_at": "2024-01-01T00:00:00Z"
            }
        })");

        auto result = ParseLoginResult(QJsonValue(doc.object()));
        QVERIFY(result.has_value());
        QCOMPARE(result->accessToken, QStringLiteral("jwt-access-token"));
        QCOMPARE(result->refreshToken, QStringLiteral("jwt-refresh-token"));
        QCOMPARE(result->tokenType, QStringLiteral("Bearer"));
        QCOMPARE(result->expiresIn, 7200);
        QCOMPARE(result->user.username, QStringLiteral("testuser"));
    }

    void parseLoginResult_missingAccessToken_returnsNullopt() {
        const auto doc = makeDoc(R"({
            "refresh_token": "ref",
            "user": {"id": 1, "username": "u", "email": "e"}
        })");
        QVERIFY(!ParseLoginResult(QJsonValue(doc.object())).has_value());
    }

    void parseLoginResult_missingUser_returnsNullopt() {
        const auto doc = makeDoc(R"({
            "access_token": "tok"
        })");
        QVERIFY(!ParseLoginResult(QJsonValue(doc.object())).has_value());
    }

    void parseLoginResult_notObject_returnsNullopt() {
        QVERIFY(!ParseLoginResult(QJsonValue("string")).has_value());
    }

    // --- ParseRefreshResult ---

    void parseRefreshResult_valid() {
        const auto doc = makeDoc(R"({
            "access_token": "new-access",
            "refresh_token": "new-refresh",
            "expires_in": 3600
        })");

        auto result = ParseRefreshResult(QJsonValue(doc.object()));
        QVERIFY(result.has_value());
        QCOMPARE(result->accessToken, QStringLiteral("new-access"));
        QCOMPARE(result->refreshToken, QStringLiteral("new-refresh"));
        QCOMPARE(result->expiresIn, 3600);
    }

    void parseRefreshResult_missingAccessToken_returnsNullopt() {
        const auto doc = makeDoc(R"({"refresh_token": "ref", "expires_in": 100})");
        QVERIFY(!ParseRefreshResult(QJsonValue(doc.object())).has_value());
    }

    void parseRefreshResult_notObject_returnsNullopt() {
        QVERIFY(!ParseRefreshResult(QJsonValue(QJsonValue::Null)).has_value());
    }
};

QTEST_APPLESS_MAIN(tst_AuthDtos)
#include "tst_AuthDtos.moc"
