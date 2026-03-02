#include <QTest>

#include <utils/ErrorCode.hpp>

using namespace disk::qml::utils;

class tst_ErrorCode : public QObject {
    Q_OBJECT

private slots:

    // --- ErrorCodeFromInt ---

    void errorCodeFromInt_knownCodes_data() {
        QTest::addColumn<int>("code");
        QTest::addColumn<ErrorCode>("expected");

        QTest::newRow("Success") << 0 << ErrorCode::Success;
        QTest::newRow("InvalidParam") << 10001 << ErrorCode::InvalidParameter;
        QTest::newRow("ValidationFailed") << 10002 << ErrorCode::ValidationFailed;
        QTest::newRow("NotFound") << 10003 << ErrorCode::ResourceNotFound;
        QTest::newRow("Conflict") << 10004 << ErrorCode::ResourceConflict;
        QTest::newRow("TooManyReqs") << 10005 << ErrorCode::TooManyRequests;
        QTest::newRow("Internal") << 10006 << ErrorCode::InternalError;
        QTest::newRow("UsernameExists") << 40001 << ErrorCode::UsernameExists;
        QTest::newRow("EmailExists") << 40002 << ErrorCode::EmailExists;
        QTest::newRow("InvalidFormat") << 40003 << ErrorCode::InvalidFormat;
        QTest::newRow("UserNotFound") << 40100 << ErrorCode::UserNotFound;
        QTest::newRow("InvalidCreds") << 40101 << ErrorCode::InvalidCredentials;
        QTest::newRow("Locked") << 40102 << ErrorCode::AccountLocked;
        QTest::newRow("Disabled") << 40103 << ErrorCode::AccountDisabled;
        QTest::newRow("InvalidToken") << 40104 << ErrorCode::InvalidToken;
        QTest::newRow("InvalidRefresh") << 40105 << ErrorCode::InvalidRefreshToken;
        QTest::newRow("TokenMissing") << 40106 << ErrorCode::TokenMissing;
        QTest::newRow("TokenMalformed") << 40107 << ErrorCode::TokenMalformed;
        QTest::newRow("TokenExpired") << 40108 << ErrorCode::TokenExpired;
        QTest::newRow("RefreshUsed") << 40110 << ErrorCode::RefreshTokenAlreadyUsed;
        QTest::newRow("TokenRevoked") << 40111 << ErrorCode::TokenRevoked;
    }

    void errorCodeFromInt_knownCodes() {
        QFETCH(int, code);
        QFETCH(ErrorCode, expected);

        auto result = ErrorCodeFromInt(code);
        QVERIFY(result.has_value());
        QCOMPARE(*result, expected);
    }

    void errorCodeFromInt_unknownCode_returnsNullopt() {
        QVERIFY(!ErrorCodeFromInt(-1).has_value());
        QVERIFY(!ErrorCodeFromInt(99999).has_value());
        QVERIFY(!ErrorCodeFromInt(1).has_value());
    }

    // --- ToUserMessage ---

    void toUserMessage_knownCodes_data() {
        QTest::addColumn<int>("code");
        QTest::addColumn<QString>("expected");

        QTest::newRow("ValidationFailed") << 10002 << QStringLiteral("参数格式不正确");
        QTest::newRow("InvalidFormat") << 40003 << QStringLiteral("参数格式不正确");
        QTest::newRow("TooManyRequests") << 10005 << QStringLiteral("请求过于频繁，请稍后再试");
        QTest::newRow("InternalError") << 10006 << QStringLiteral("服务器错误，请稍后重试");
        QTest::newRow("UsernameExists") << 40001 << QStringLiteral("用户名已被注册");
        QTest::newRow("EmailExists") << 40002 << QStringLiteral("邮箱已被注册");
        QTest::newRow("InvalidCredentials") << 40101 << QStringLiteral("用户名或密码错误");
        QTest::newRow("AccountLocked") << 40102 << QStringLiteral("账户已锁定，请15分钟后重试");
        QTest::newRow("AccountDisabled") << 40103 << QStringLiteral("账户已被禁用");
        QTest::newRow("TokenExpired") << 40108 << QStringLiteral("令牌已过期");
    }

    void toUserMessage_knownCodes() {
        QFETCH(int, code);
        QFETCH(QString, expected);

        QCOMPARE(ToUserMessage(code, QString{}), expected);
    }

    void toUserMessage_unknownCode_usesFallback() {
        const QString fallback = QStringLiteral("server says oops");
        QCOMPARE(ToUserMessage(77777, fallback), fallback);
    }

    void toUserMessage_unknownCode_emptyFallback_returnsDefault() {
        QCOMPARE(ToUserMessage(77777, QString{}), QStringLiteral("未知错误"));
    }

    void toUserMessage_knownCode_ignoresFallback() {
        // Even when a fallback is provided, known codes return the mapped message
        QCOMPARE(ToUserMessage(40001, QStringLiteral("ignored")), QStringLiteral("用户名已被注册"));
    }
};

QTEST_APPLESS_MAIN(tst_ErrorCode)
#include "tst_ErrorCode.moc"
