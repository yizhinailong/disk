#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>
#include <functional>
#include <memory>

#include <api/ApiClient.hpp>
#include <api/ApiReply.hpp>
#include <api/AuthApi.hpp>
#include <models/AuthDtos.hpp>

using namespace disk::qml;

// ---------------------------------------------------------------------------
// FakeApiClient — injectable lambdas for PostJson / PostJsonWithBearerToken
// ---------------------------------------------------------------------------
class FakeApiClient : public api::ApiClient {
public:
    std::function<void(const QString&, const QJsonObject&, QObject*, api::PostJsonCallback)> postJsonHandler;
    std::function<void(const QString&, const QJsonObject&, const QString&, QObject*, api::PostJsonCallback)> postJsonWithBearerTokenHandler;

    int postJsonCallCount = 0;
    int postJsonWithBearerTokenCallCount = 0;
    QString lastBearerToken;

    auto SetBaseUrl(const QUrl&) -> void override {}

    auto SetBearerToken(const QString&) -> void override {}

    auto PostJson(const QString& path, const QJsonObject& body, QObject* ctx, api::PostJsonCallback cb) -> void override {
        postJsonCallCount++;
        if (postJsonHandler) {
            postJsonHandler(path, body, ctx, std::move(cb));
        }
    }

    auto PostJsonWithBearerToken(const QString& path, const QJsonObject& body, const QString& bearerToken, QObject* ctx, api::PostJsonCallback cb) -> void override {
        postJsonWithBearerTokenCallCount++;
        lastBearerToken = bearerToken;
        if (postJsonWithBearerTokenHandler) {
            postJsonWithBearerTokenHandler(path, body, bearerToken, ctx, std::move(cb));
        }
    }
};

// ---------------------------------------------------------------------------
// Helper: build an ApiReply with a JSON body
// ---------------------------------------------------------------------------
static auto makeJsonReply(const QJsonObject& obj, int httpStatus = 200) -> api::ApiReply {
    api::ApiReply reply;
    reply.error.hasNetworkError = false;
    reply.error.httpStatus = httpStatus;
    reply.error.httpStatusSuccess = (httpStatus >= 200 && httpStatus < 300);
    const QByteArray raw = QJsonDocument(obj).toJson(QJsonDocument::Compact);
    reply.body = raw;
    reply.json = QJsonDocument::fromJson(raw, &reply.jsonParseError);
    return reply;
}

static auto makeNetworkErrorReply(const QString& errStr) -> api::ApiReply {
    api::ApiReply reply;
    reply.error.hasNetworkError = true;
    reply.error.networkError = QNetworkReply::ConnectionRefusedError;
    reply.error.networkErrorString = errStr;
    return reply;
}

static auto makeRawBodyReply(const QByteArray& raw, int httpStatus = 200) -> api::ApiReply {
    api::ApiReply reply;
    reply.error.hasNetworkError = false;
    reply.error.httpStatus = httpStatus;
    reply.error.httpStatusSuccess = (httpStatus >= 200 && httpStatus < 300);
    reply.body = raw;
    // Only set json optional if parse succeeds — mirrors ApiClient behaviour
    QJsonParseError parseErr;
    QJsonDocument doc = QJsonDocument::fromJson(raw, &parseErr);
    reply.jsonParseError = parseErr;
    if (parseErr.error == QJsonParseError::NoError) {
        reply.json = std::move(doc);
    }
    return reply;
}

// ---------------------------------------------------------------------------
// Helper: build a valid success envelope JSON object
// ---------------------------------------------------------------------------
static auto makeEnvelopeObj(int code, const QString& msg, const QJsonValue& data = QJsonValue{}) -> QJsonObject {
    QJsonObject obj;
    obj["code"] = code;
    obj["message"] = msg;
    if (!data.isNull() && !data.isUndefined()) {
        obj["data"] = data;
    }
    return obj;
}

// ===========================================================================
class tst_AuthApi : public QObject {
    Q_OBJECT

private:
    FakeApiClient m_fake_client;
    std::unique_ptr<api::AuthApi> m_auth_api;

private slots:

    void initTestCase() {
        QCoreApplication::setOrganizationName("DiskTest");
        QCoreApplication::setApplicationName("diskqml-authapi-test");
    }

    void init() {
        m_fake_client.postJsonHandler = nullptr;
        m_fake_client.postJsonWithBearerTokenHandler = nullptr;
        m_fake_client.postJsonCallCount = 0;
        m_fake_client.postJsonWithBearerTokenCallCount = 0;
        m_fake_client.lastBearerToken.clear();
        m_auth_api = std::make_unique<api::AuthApi>(&m_fake_client);
    }

    void cleanup() {
        m_auth_api.reset();
    }

    // -----------------------------------------------------------------------
    // 1. success_parsesEnvelope
    //    HTTP 200 with valid code=0 envelope → networkError empty, envelope parsed
    // -----------------------------------------------------------------------
    void success_parsesEnvelope() {
        QJsonObject envelopeObj = makeEnvelopeObj(0, QStringLiteral("success"));

        m_fake_client.postJsonHandler = [&](auto, auto, auto, api::PostJsonCallback cb) {
            cb(makeJsonReply(envelopeObj, 200));
        };

        bool called = false;
        m_auth_api->Register(
            QStringLiteral("user"),
            QStringLiteral("user@example.com"),
            QStringLiteral("Password1"),
            this,
            [&](models::ApiEnvelope env, QString netErr) {
                called = true;
                QVERIFY(netErr.isEmpty());
                QCOMPARE(env.code, 0);
                QCOMPARE(env.message, QStringLiteral("success"));
            }
        );
        QVERIFY(called);
        QCOMPARE(m_fake_client.postJsonCallCount, 1);
    }

    // -----------------------------------------------------------------------
    // 2. http400_withEnvelope_stillParses
    //    HTTP 400 with JSON envelope (code != 0) → envelope still parsed, networkError empty
    // -----------------------------------------------------------------------
    void http400_withEnvelope_stillParses() {
        QJsonObject envelopeObj = makeEnvelopeObj(40001, QStringLiteral("username exists"));

        m_fake_client.postJsonHandler = [&](auto, auto, auto, api::PostJsonCallback cb) {
            cb(makeJsonReply(envelopeObj, 400));
        };

        bool called = false;
        m_auth_api->Register(
            QStringLiteral("user"),
            QStringLiteral("user@example.com"),
            QStringLiteral("Password1"),
            this,
            [&](models::ApiEnvelope env, QString netErr) {
                called = true;
                QVERIFY(netErr.isEmpty());
                QCOMPARE(env.code, 40001);
                QCOMPARE(env.message, QStringLiteral("username exists"));
            }
        );
        QVERIFY(called);
    }

    // -----------------------------------------------------------------------
    // 3. networkError_returnsErrorString
    //    Transport error → networkError non-empty
    // -----------------------------------------------------------------------
    void networkError_returnsErrorString() {
        m_fake_client.postJsonHandler = [](auto, auto, auto, api::PostJsonCallback cb) {
            cb(makeNetworkErrorReply(QStringLiteral("connection refused")));
        };

        bool called = false;
        m_auth_api->Login(
            QStringLiteral("user"),
            QStringLiteral("Password1"),
            this,
            [&](models::ApiEnvelope, QString netErr) {
                called = true;
                QVERIFY(!netErr.isEmpty());
                QCOMPARE(netErr, QStringLiteral("connection refused"));
            }
        );
        QVERIFY(called);
    }

    // -----------------------------------------------------------------------
    // 4. parseError_returnsErrorMessage
    //    Body is not valid JSON → error contains parse failure message
    // -----------------------------------------------------------------------
    void parseError_returnsErrorMessage() {
        m_fake_client.postJsonHandler = [](auto, auto, auto, api::PostJsonCallback cb) {
            cb(makeRawBodyReply(QByteArrayLiteral("not-json-at-all"), 200));
        };

        bool called = false;
        m_auth_api->Login(
            QStringLiteral("user"),
            QStringLiteral("Password1"),
            this,
            [&](models::ApiEnvelope, QString netErr) {
                called = true;
                QVERIFY(!netErr.isEmpty());
                QVERIFY2(netErr.contains(QStringLiteral("parse"), Qt::CaseInsensitive) || netErr.contains(QStringLiteral("JSON"), Qt::CaseInsensitive) || netErr.contains(QStringLiteral("Failed"), Qt::CaseInsensitive), qPrintable(netErr));
            }
        );
        QVERIFY(called);
    }

    // -----------------------------------------------------------------------
    // 5. invalidEnvelope_returnsErrorMessage
    //    JSON body missing code/message fields → error contains invalid envelope message
    // -----------------------------------------------------------------------
    void invalidEnvelope_returnsErrorMessage() {
        // JSON that parses but lacks "code" and "message"
        QJsonObject badEnvelope;
        badEnvelope["result"] = "ok";
        badEnvelope["status"] = 1;

        m_fake_client.postJsonHandler = [&](auto, auto, auto, api::PostJsonCallback cb) {
            cb(makeJsonReply(badEnvelope, 200));
        };

        bool called = false;
        m_auth_api->Refresh(
            QStringLiteral("some-refresh-token"),
            this,
            [&](models::ApiEnvelope, QString netErr) {
                called = true;
                QVERIFY(!netErr.isEmpty());
                QVERIFY2(netErr.contains(QStringLiteral("envelope"), Qt::CaseInsensitive) || netErr.contains(QStringLiteral("Invalid"), Qt::CaseInsensitive) || netErr.contains(QStringLiteral("format"), Qt::CaseInsensitive), qPrintable(netErr));
            }
        );
        QVERIFY(called);
    }

    // -----------------------------------------------------------------------
    // 6. logout_usesBearerTokenOverride
    //    Logout calls PostJsonWithBearerToken (not PostJson + SetBearerToken)
    //    and passes the access token as the bearer token
    // -----------------------------------------------------------------------
    void logout_usesBearerTokenOverride() {
        const QString accessToken = QStringLiteral("my-access-token-xyz");
        QJsonObject envelopeObj = makeEnvelopeObj(0, QStringLiteral("success"));

        m_fake_client.postJsonWithBearerTokenHandler = [&](auto, auto, auto, auto, api::PostJsonCallback cb) {
            cb(makeJsonReply(envelopeObj, 200));
        };

        bool called = false;
        m_auth_api->Logout(
            accessToken,
            this,
            [&](models::ApiEnvelope env, QString netErr) {
                called = true;
                QVERIFY(netErr.isEmpty());
                QCOMPARE(env.code, 0);
            }
        );

        QVERIFY(called);
        // Must use PostJsonWithBearerToken, never PostJson
        QCOMPARE(m_fake_client.postJsonWithBearerTokenCallCount, 1);
        QCOMPARE(m_fake_client.postJsonCallCount, 0);
        // The bearer token passed must be the access token
        QCOMPARE(m_fake_client.lastBearerToken, accessToken);
    }
};

QTEST_MAIN(tst_AuthApi)
#include "tst_AuthApi.moc"
