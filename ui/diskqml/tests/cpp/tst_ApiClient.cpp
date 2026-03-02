#include <QCoreApplication>
#include <QEventLoop>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTest>
#include <QTimer>

#include <api/ApiClient.hpp>

using namespace disk::qml;

// ---------------------------------------------------------------------------
// tst_ApiClient — integration tests using a real QTcpServer
// These tests verify that ApiClient correctly handles non-2xx JSON bodies
// (the core bug fix: HTTP 4xx responses with JSON are preserved, not discarded)
// ---------------------------------------------------------------------------
class tst_ApiClient : public QObject {
    Q_OBJECT

private:
    QTcpServer* m_server = nullptr;
    quint16 m_port = 0;

    void sendHttpResponse(QTcpSocket* sock, int status, const QByteArray& body, const QByteArray& contentType = "application/json") {
        QString statusText;
        if (status == 200) {
            statusText = QStringLiteral("OK");
        } else if (status == 400) {
            statusText = QStringLiteral("Bad Request");
        } else if (status == 500) {
            statusText = QStringLiteral("Internal Server Error");
        } else {
            statusText = QStringLiteral("Unknown");
        }

        QByteArray response =
            QString("HTTP/1.1 %1 %2\r\n" "Content-Type: %3\r\n" "Content-Length: %4\r\n" "Connection: close\r\n" "\r\n")
                .arg(status)
                .arg(statusText)
                .arg(QString::fromUtf8(contentType))
                .arg(body.size())
                .toUtf8();
        response += body;
        sock->write(response);
        sock->flush();
    }

private slots:

    void initTestCase() {
        QCoreApplication::setOrganizationName("DiskTest");
        QCoreApplication::setApplicationName("diskqml-apiclient-test");

        m_server = new QTcpServer(this);
        QVERIFY(m_server->listen(QHostAddress::LocalHost));
        m_port = m_server->serverPort();
    }

    void cleanupTestCase() {
        m_server->close();
    }

    // -----------------------------------------------------------------------
    // 1. http400_jsonBody_preservesJson
    //    HTTP 400 with JSON envelope → json optional has value, status=400, no network error
    // -----------------------------------------------------------------------
    void http400_jsonBody_preservesJson() {
        const QByteArray body = R"({"code":40101,"message":"invalid","data":null})";

        // Serve one connection
        QObject connGuard;
        connect(m_server, &QTcpServer::newConnection, &connGuard, [&]() {
            QTcpSocket* sock = m_server->nextPendingConnection();
            connect(sock, &QTcpSocket::readyRead, sock, [this, sock, &body]() {
                // Consume the request then respond
                sock->readAll();
                sendHttpResponse(sock, 400, body);
                sock->disconnectFromHost();
            });
        });

        api::ApiClient client;
        client.SetBaseUrl(QUrl(QString("http://127.0.0.1:%1").arg(m_port)));

        bool callbackFired = false;
        QEventLoop loop;

        client.PostJson(QStringLiteral("/test"), QJsonObject{}, this, [&](bool hasNetworkError, QString, int httpStatus, QByteArray responseBody) {
            callbackFired = true;

            QCOMPARE(httpStatus, 400);
            QCOMPARE(hasNetworkError, false);
            QJsonParseError parseError;
            const QJsonDocument json = QJsonDocument::fromJson(responseBody, &parseError);
            QCOMPARE(parseError.error, QJsonParseError::NoError);
            QCOMPARE(json.object()["code"].toInt(), 40101);
            QCOMPARE(json.object()["message"].toString(), QStringLiteral("invalid"));

            loop.quit();
        });

        // Guard: timeout after 5 s to avoid hanging the test suite
        QTimer::singleShot(5000, &loop, [&loop]() { loop.exit(1); });
        const int exitCode = loop.exec();

        // Disconnect the slot so next test doesn't accidentally receive it
        disconnect(m_server, &QTcpServer::newConnection, &connGuard, nullptr);

        QCOMPARE(exitCode, 0);
        QVERIFY(callbackFired);
    }

    // -----------------------------------------------------------------------
    // 2. http200_jsonBody_preservesJson
    //    HTTP 200 with JSON body → json optional has value, status=200
    // -----------------------------------------------------------------------
    void http200_jsonBody_preservesJson() {
        const QByteArray body = R"({"code":0,"message":"success","data":{}})";

        QObject connGuard;
        connect(m_server, &QTcpServer::newConnection, &connGuard, [&]() {
            QTcpSocket* sock = m_server->nextPendingConnection();
            connect(sock, &QTcpSocket::readyRead, sock, [this, sock, &body]() {
                sock->readAll();
                sendHttpResponse(sock, 200, body);
                sock->disconnectFromHost();
            });
        });

        api::ApiClient client;
        client.SetBaseUrl(QUrl(QString("http://127.0.0.1:%1").arg(m_port)));

        bool callbackFired = false;
        QEventLoop loop;

        client.PostJson(QStringLiteral("/test"), QJsonObject{}, this, [&](bool hasNetworkError, QString, int httpStatus, QByteArray responseBody) {
            callbackFired = true;

            QCOMPARE(httpStatus, 200);
            QCOMPARE(hasNetworkError, false);
            QJsonParseError parseError;
            const QJsonDocument json = QJsonDocument::fromJson(responseBody, &parseError);
            QCOMPARE(parseError.error, QJsonParseError::NoError);
            QCOMPARE(json.object()["code"].toInt(), 0);
            QCOMPARE(json.object()["message"].toString(), QStringLiteral("success"));

            loop.quit();
        });

        QTimer::singleShot(5000, &loop, [&loop]() { loop.exit(1); });
        const int exitCode = loop.exec();

        disconnect(m_server, &QTcpServer::newConnection, &connGuard, nullptr);

        QCOMPARE(exitCode, 0);
        QVERIFY(callbackFired);
    }

    // -----------------------------------------------------------------------
    // 3. http500_nonJson_setsParseError
    //    HTTP 500 with plain-text body → json optional is empty, jsonParseError is set
    // -----------------------------------------------------------------------
    void http500_nonJson_setsParseError() {
        const QByteArray body = "Internal Server Error";

        QObject connGuard;
        connect(m_server, &QTcpServer::newConnection, &connGuard, [&]() {
            QTcpSocket* sock = m_server->nextPendingConnection();
            connect(sock, &QTcpSocket::readyRead, sock, [this, sock, &body]() {
                sock->readAll();
                sendHttpResponse(sock, 500, body, "text/plain");
                sock->disconnectFromHost();
            });
        });

        api::ApiClient client;
        client.SetBaseUrl(QUrl(QString("http://127.0.0.1:%1").arg(m_port)));

        bool callbackFired = false;
        QEventLoop loop;

        client.PostJson(QStringLiteral("/test"), QJsonObject{}, this, [&](bool, QString, int httpStatus, QByteArray responseBody) {
            callbackFired = true;

            QCOMPARE(httpStatus, 500);
            QJsonParseError parseError;
            QJsonDocument::fromJson(responseBody, &parseError);
            QVERIFY2(parseError.error != QJsonParseError::NoError, "jsonParseError must be set when body is not valid JSON");

            loop.quit();
        });

        QTimer::singleShot(5000, &loop, [&loop]() { loop.exit(1); });
        const int exitCode = loop.exec();

        disconnect(m_server, &QTcpServer::newConnection, &connGuard, nullptr);

        QCOMPARE(exitCode, 0);
        QVERIFY(callbackFired);
    }
};

QTEST_MAIN(tst_ApiClient)
#include "tst_ApiClient.moc"
