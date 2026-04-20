#include <QJsonObject>
#include <QTest>

#include "network/ErrorAdapter.hpp"

using namespace disk::desktop;

class TestErrorAdapter : public QObject {
    Q_OBJECT

private slots:

    void TokenExpiredIsRetryable() {
        QJsonObject json;
        json["code"] = 40108;
        json["message"] = "Access token已过期";

        auto err = ErrorAdapter::FromJson(json);
        QCOMPARE(err.code, 40108);
        QCOMPARE(err.family, QString("auth"));
        QCOMPARE(err.category, QString("SessionExpired"));
        QVERIFY(err.retryable);
        QCOMPARE(err.action, QString("refresh_owner_session_or_reverify_share"));
    }

    void InvalidRefreshTokenNotRetryable() {
        QJsonObject json;
        json["code"] = 40105;
        json["message"] = "Refresh token无效或已使用";

        auto err = ErrorAdapter::FromJson(json);
        QCOMPARE(err.code, 40105);
        QCOMPARE(err.family, QString("auth"));
        QCOMPARE(err.category, QString("ReLoginRequired"));
        QVERIFY(!err.retryable);
        QCOMPARE(err.action, QString("clear_session_and_login"));
    }

    void StorageQuotaExceededNotRetryable() {
        QJsonObject json;
        json["code"] = 50004;
        json["message"] = "存储配额不足";

        auto err = ErrorAdapter::FromJson(json);
        QCOMPARE(err.code, 50004);
        QCOMPARE(err.family, QString("file"));
        QCOMPARE(err.category, QString("StorageQuotaExceeded"));
        QVERIFY(!err.retryable);
        QCOMPARE(err.action, QString("show_storage_and_stop"));
    }

    void ShareAccessDeniedNotRetryable() {
        QJsonObject json;
        json["code"] = 60004;
        json["message"] = "没有下载权限";

        auto err = ErrorAdapter::FromJson(json);
        QCOMPARE(err.code, 60004);
        QCOMPARE(err.family, QString("share"));
        QCOMPARE(err.category, QString("PermissionDenied"));
        QVERIFY(!err.retryable);
        QCOMPARE(err.action, QString("disable_download"));
    }

    void TooManyRequestsIsRetryable() {
        QJsonObject json;
        json["code"] = 10005;
        json["message"] = "请求过于频繁";

        auto err = ErrorAdapter::FromJson(json);
        QCOMPARE(err.code, 10005);
        QCOMPARE(err.family, QString("general"));
        QCOMPARE(err.category, QString("RateLimited"));
        QVERIFY(err.retryable);
        QCOMPARE(err.action, QString("wait_and_retry"));
    }

    void FieldAndValueExtraction() {
        QJsonObject json;
        json["code"] = 10001;
        json["message"] = "Validation failed";
        json["field"] = "username";
        json["value"] = "ab";

        auto err = ErrorAdapter::FromJson(json);
        QVERIFY(err.field.has_value());
        QCOMPARE(*err.field, QString("username"));
        QVERIFY(err.value.has_value());
        QCOMPARE(*err.value, QString("ab"));
    }

    void MissingFieldAndValueAreNullopt() {
        QJsonObject json;
        json["code"] = 40101;
        json["message"] = "Credentials rejected";

        auto err = ErrorAdapter::FromJson(json);
        QVERIFY(!err.field.has_value());
        QVERIFY(!err.value.has_value());
    }

    void NetworkErrorTimeoutIsRetryable() {
        auto err = ErrorAdapter::FromNetworkError(QNetworkReply::TimeoutError);
        QCOMPARE(err.family, QString("network"));
        QCOMPARE(err.category, QString("Timeout"));
        QVERIFY(err.retryable);
        QCOMPARE(err.action, QString("retry_with_backoff"));
    }

    void NetworkErrorConnectionRefusedIsRetryable() {
        auto err = ErrorAdapter::FromNetworkError(QNetworkReply::ConnectionRefusedError);
        QCOMPARE(err.family, QString("network"));
        QVERIFY(err.retryable);
    }

    void NetworkErrorSslNotRetryable() {
        auto err = ErrorAdapter::FromNetworkError(QNetworkReply::SslHandshakeFailedError);
        QCOMPARE(err.category, QString("TlsError"));
        QVERIFY(!err.retryable);
    }

    void ServerFailure10006IsRetryable() {
        QJsonObject json;
        json["code"] = 10006;
        json["message"] = "Internal server error";

        auto err = ErrorAdapter::FromJson(json);
        QCOMPARE(err.family, QString("general"));
        QCOMPARE(err.category, QString("ServerFailure"));
        QVERIFY(err.retryable);
    }
};

#include "test_error_adapter.moc"
