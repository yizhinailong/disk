#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTcpServer>
#include <QTcpSocket>
#include <QSignalSpy>
#include <QTest>

#include "helpers/MockNetworkAccessManager.hpp"
#include "managers/DriveManager.hpp"
#include "network/NetworkClient.hpp"
#include "network/RequestFactory.hpp"

using namespace disk::desktop;
using namespace disk::desktop::testing;
using namespace disk::desktop::managers;

namespace {

    class OneShotHttpServer : public QObject {
    public:
        explicit OneShotHttpServer(
            QByteArray responseBody,
            int statusCode = 200,
            QByteArray reasonPhrase = "OK",
            QObject* parent = nullptr
        )
            : QObject(parent),
              m_response_body(std::move(responseBody)),
              m_status_code(statusCode),
              m_reason_phrase(std::move(reasonPhrase)) {
            connect(&m_server, &QTcpServer::newConnection, this, [this]() {
                while (m_server.hasPendingConnections()) {
                    auto* socket = m_server.nextPendingConnection();
                    connect(socket, &QTcpSocket::readyRead, this, [this, socket]() {
                        m_request_data.append(socket->readAll());
                        if (m_response_sent) {
                            return;
                        }

                        m_response_sent = true;

                        QByteArray response;
                        response += "HTTP/1.1 ";
                        response += QByteArray::number(m_status_code);
                        response += " ";
                        response += m_reason_phrase;
                        response += "\r\n";
                        response += "Content-Type: application/json\r\n";
                        response += "Content-Length: ";
                        response += QByteArray::number(m_response_body.size());
                        response += "\r\n";
                        response += "Connection: close\r\n\r\n";
                        response += m_response_body;

                        socket->write(response);
                        socket->disconnectFromHost();
                    });
                    connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
                }
            });
        }

        auto Listen() -> bool {
            return m_server.listen(QHostAddress::LocalHost, 0);
        }

        auto BaseUrl() const -> QString {
            return QString("http://127.0.0.1:%1/").arg(m_server.serverPort());
        }

        auto RequestData() const -> QByteArray {
            return m_request_data;
        }

    private:
        QTcpServer m_server;
        QByteArray m_response_body;
        QByteArray m_request_data;
        int m_status_code{ 200 };
        QByteArray m_reason_phrase;
        bool m_response_sent{ false };
    };

} // namespace

class TestDriveManager : public QObject {
    Q_OBJECT

private slots:

    void InitHasListModelAndTreeModel() {
        MockNetworkAccessManager mock_network;
        NetworkClient network_client(static_cast<QNetworkAccessManager*>(&mock_network));
        RequestFactory request_factory;
        DriveManager mgr(&network_client, &request_factory);

        QVERIFY(mgr.listModel() != nullptr);
        QVERIFY(mgr.treeModel() != nullptr);
        QCOMPARE(mgr.listModel()->rowCount(), 0);
        QCOMPARE(mgr.treeModel()->rowCount(), 0);
    }

    void ListFilesPopulatesListModel() {
        MockNetworkAccessManager mock_network;
        mock_network.RegisterResponse(
            "api/file/list",
            QJsonObject{
                { "code", 0 },
                { "message", "success" },
                { "data",
                  QJsonObject{
                      { "items",
                       QJsonArray{
                           QJsonObject{
                               { "id", 1.0 },
                               { "type", "file" },
                               { "name", "report.pdf" },
                               { "size", 1024.0 },
                           },
                           QJsonObject{
                               { "id", 2.0 },
                               { "type", "folder" },
                               { "name", "Documents" },
                           },
                       } },
                      { "pagination",
                       QJsonObject{
                           { "page", 1 },
                           { "page_size", 50 },
                           { "total", 2 },
                           { "total_pages", 1 },
                       } },
                  } },
            }
        );

        NetworkClient network_client(static_cast<QNetworkAccessManager*>(&mock_network));
        network_client.SetBaseUrl("http://127.0.0.1:8080/");
        RequestFactory request_factory;
        request_factory.SetOwnerAccessToken("test_token");
        DriveManager mgr(&network_client, &request_factory);

        QSignalSpy pagination_spy(&mgr, &DriveManager::paginationLoaded);
        QSignalSpy error_spy(&mgr, &DriveManager::apiError);

        mgr.listFiles("0");

        QTRY_COMPARE(pagination_spy.count(), 1);
        QCOMPARE(error_spy.count(), 0);

        QCOMPARE(mgr.listModel()->rowCount(), 2);
        QCOMPARE(
            mgr.listModel()->data(mgr.listModel()->index(0), DriveListModel::NameRole).toString(),
            QString("report.pdf")
        );
        QCOMPARE(
            mgr.listModel()->data(mgr.listModel()->index(1), DriveListModel::NameRole).toString(),
            QString("Documents")
        );
    }

    void SearchFilesPopulatesListModelWithSearchOrigin() {
        MockNetworkAccessManager mock_network;
        mock_network.RegisterResponse(
            "api/file/search",
            QJsonObject{
                { "code", 0 },
                { "message", "success" },
                { "data",
                  QJsonObject{
                      { "items",
                       QJsonArray{
                           QJsonObject{
                               { "id", 10.0 },
                               { "type", "file" },
                               { "name", "notes.md" },
                               { "size", 512.0 },
                               { "path", "/docs/notes.md" },
                           },
                       } },
                      { "pagination",
                       QJsonObject{
                           { "page", 1 },
                           { "page_size", 50 },
                           { "total", 1 },
                           { "total_pages", 1 },
                       } },
                  } },
            }
        );

        NetworkClient network_client(static_cast<QNetworkAccessManager*>(&mock_network));
        network_client.SetBaseUrl("http://127.0.0.1:8080/");
        RequestFactory request_factory;
        request_factory.SetOwnerAccessToken("test_token");
        DriveManager mgr(&network_client, &request_factory);

        QSignalSpy pagination_spy(&mgr, &DriveManager::paginationLoaded);

        mgr.searchFiles("notes");

        QTRY_COMPARE(pagination_spy.count(), 1);
        QCOMPARE(mgr.listModel()->rowCount(), 1);
        QCOMPARE(
            mgr.listModel()->data(mgr.listModel()->index(0), DriveListModel::OriginRole).toString(),
            QString("search")
        );
    }

    void GetFileDetailEmitsDetail() {
        MockNetworkAccessManager mock_network;
        mock_network.RegisterResponse(
            "api/file/42",
            QJsonObject{
                { "code", 0 },
                { "message", "success" },
                { "data",
                  QJsonObject{
                      { "id", 42.0 },
                      { "name", "detail.pdf" },
                      { "type", "file" },
                      { "parent_id", 0.0 },
                      { "size", 2048.0 },
                      { "mime_type", "application/pdf" },
                      { "hash", "abc123" },
                  } },
            }
        );

        NetworkClient network_client(static_cast<QNetworkAccessManager*>(&mock_network));
        network_client.SetBaseUrl("http://127.0.0.1:8080/");
        RequestFactory request_factory;
        request_factory.SetOwnerAccessToken("test_token");
        DriveManager mgr(&network_client, &request_factory);

        QSignalSpy detail_spy(&mgr, &DriveManager::fileDetailLoaded);

        mgr.getFileDetail("42");

        QTRY_COMPARE(detail_spy.count(), 1);
        auto detail = detail_spy.takeFirst().at(0).toMap();
        QCOMPARE(detail.value("name").toString(), QString("detail.pdf"));
        QCOMPARE(detail.value("type").toString(), QString("file"));
    }

    void CreateFolderEmitsSuccess() {
        MockNetworkAccessManager mock_network;
        mock_network.RegisterResponse(
            "api/folder/create",
            QJsonObject{
                { "code", 0 },
                { "message", "success" },
                { "data", QJsonObject{} },
            }
        );

        NetworkClient network_client(static_cast<QNetworkAccessManager*>(&mock_network));
        network_client.SetBaseUrl("http://127.0.0.1:8080/");
        RequestFactory request_factory;
        request_factory.SetOwnerAccessToken("test_token");
        DriveManager mgr(&network_client, &request_factory);

        QSignalSpy success_spy(&mgr, &DriveManager::operationSuccess);
        QSignalSpy error_spy(&mgr, &DriveManager::apiError);

        mgr.createFolder("0", "NewFolder");

        QTRY_COMPARE(success_spy.count(), 1);
        QCOMPARE(error_spy.count(), 0);
    }

    void CreateFolderRejectsMalformedSuccessPayload() {
        MockNetworkAccessManager mock_network;
        mock_network.RegisterRawResponse("api/folder/create", "not-json", 200);

        NetworkClient network_client(static_cast<QNetworkAccessManager*>(&mock_network));
        network_client.SetBaseUrl("http://127.0.0.1:8080/");
        RequestFactory request_factory;
        request_factory.SetOwnerAccessToken("test_token");
        DriveManager mgr(&network_client, &request_factory);

        QSignalSpy success_spy(&mgr, &DriveManager::operationSuccess);
        QSignalSpy error_spy(&mgr, &DriveManager::apiError);

        mgr.createFolder("0", "NewFolder");

        QTRY_COMPARE(error_spy.count(), 1);
        QCOMPARE(success_spy.count(), 0);

        auto arguments = error_spy.takeFirst();
        QCOMPARE(arguments.at(0).toString(), QString("Invalid response format"));
        QCOMPARE(arguments.at(1).toInt(), 0);
    }

    void CreateFolderRejectsApiErrorEnvelopeOnHttp200() {
        MockNetworkAccessManager mock_network;
        mock_network.RegisterResponse(
            "api/folder/create",
            QJsonObject{
                { "code", 50002 },
                { "message", "Invalid filename" },
                { "data", QJsonValue::Null },
            },
            200
        );

        NetworkClient network_client(static_cast<QNetworkAccessManager*>(&mock_network));
        network_client.SetBaseUrl("http://127.0.0.1:8080/");
        RequestFactory request_factory;
        request_factory.SetOwnerAccessToken("test_token");
        DriveManager mgr(&network_client, &request_factory);

        QSignalSpy success_spy(&mgr, &DriveManager::operationSuccess);
        QSignalSpy error_spy(&mgr, &DriveManager::apiError);

        mgr.createFolder("0", "NewFolder");

        QTRY_COMPARE(error_spy.count(), 1);
        QCOMPARE(success_spy.count(), 0);

        auto arguments = error_spy.takeFirst();
        QCOMPARE(arguments.at(0).toString(), QString("Invalid filename"));
        QCOMPARE(arguments.at(1).toInt(), 50002);
    }

    void RenameItemEmitsSuccess() {
        MockNetworkAccessManager mock_network;
        mock_network.RegisterResponse(
            "api/file/10/rename",
            QJsonObject{
                { "code", 0 },
                { "message", "success" },
                { "data", QJsonObject{} },
            }
        );

        NetworkClient network_client(static_cast<QNetworkAccessManager*>(&mock_network));
        network_client.SetBaseUrl("http://127.0.0.1:8080/");
        RequestFactory request_factory;
        request_factory.SetOwnerAccessToken("test_token");
        DriveManager mgr(&network_client, &request_factory);

        QSignalSpy success_spy(&mgr, &DriveManager::operationSuccess);

        mgr.renameItem("10", "renamed.txt");

        QTRY_COMPARE(success_spy.count(), 1);
    }

    void RenameItemRejectsMalformedSuccessPayload() {
        MockNetworkAccessManager mock_network;
        mock_network.RegisterRawResponse("api/file/10/rename", "not-json", 200);

        NetworkClient network_client(static_cast<QNetworkAccessManager*>(&mock_network));
        network_client.SetBaseUrl("http://127.0.0.1:8080/");
        RequestFactory request_factory;
        request_factory.SetOwnerAccessToken("test_token");
        DriveManager mgr(&network_client, &request_factory);

        QSignalSpy success_spy(&mgr, &DriveManager::operationSuccess);
        QSignalSpy error_spy(&mgr, &DriveManager::apiError);

        mgr.renameItem("10", "renamed.txt");

        QTRY_COMPARE(error_spy.count(), 1);
        QCOMPARE(success_spy.count(), 0);

        auto arguments = error_spy.takeFirst();
        QCOMPARE(arguments.at(0).toString(), QString("Invalid response format"));
        QCOMPARE(arguments.at(1).toInt(), 0);
    }

    void RenameItemRejectsApiErrorEnvelopeOnHttp200() {
        MockNetworkAccessManager mock_network;
        mock_network.RegisterResponse(
            "api/file/10/rename",
            QJsonObject{
                { "code", 50010 },
                { "message", "Name already exists" },
                { "data", QJsonValue::Null },
            },
            200
        );

        NetworkClient network_client(static_cast<QNetworkAccessManager*>(&mock_network));
        network_client.SetBaseUrl("http://127.0.0.1:8080/");
        RequestFactory request_factory;
        request_factory.SetOwnerAccessToken("test_token");
        DriveManager mgr(&network_client, &request_factory);

        QSignalSpy success_spy(&mgr, &DriveManager::operationSuccess);
        QSignalSpy error_spy(&mgr, &DriveManager::apiError);

        mgr.renameItem("10", "renamed.txt");

        QTRY_COMPARE(error_spy.count(), 1);
        QCOMPARE(success_spy.count(), 0);

        auto arguments = error_spy.takeFirst();
        QCOMPARE(arguments.at(0).toString(), QString("Name already exists"));
        QCOMPARE(arguments.at(1).toInt(), 50010);
    }

    void DeleteItemsEmitsSuccessWithValidEnvelope() {
        OneShotHttpServer server(
            QJsonDocument(
                QJsonObject{
                    { "code", 0 },
                    { "message", "success" },
                    { "data", QJsonObject{} },
                }
            )
                .toJson(QJsonDocument::Compact)
        );
        QVERIFY(server.Listen());

        MockNetworkAccessManager mock_network;
        NetworkClient network_client(static_cast<QNetworkAccessManager*>(&mock_network));
        network_client.SetBaseUrl(server.BaseUrl());
        RequestFactory request_factory;
        request_factory.SetOwnerAccessToken("test_token");
        DriveManager mgr(&network_client, &request_factory);

        QSignalSpy success_spy(&mgr, &DriveManager::operationSuccess);
        QSignalSpy error_spy(&mgr, &DriveManager::apiError);

        mgr.deleteItems({ "10" });

        QTRY_COMPARE(success_spy.count(), 1);
        QCOMPARE(error_spy.count(), 0);
        QVERIFY(server.RequestData().contains("DELETE /api/file HTTP/1.1"));
    }

    void DeleteItemsRejectsMalformedSuccessPayload() {
        OneShotHttpServer server("not-json");
        QVERIFY(server.Listen());

        MockNetworkAccessManager mock_network;
        NetworkClient network_client(static_cast<QNetworkAccessManager*>(&mock_network));
        network_client.SetBaseUrl(server.BaseUrl());
        RequestFactory request_factory;
        request_factory.SetOwnerAccessToken("test_token");
        DriveManager mgr(&network_client, &request_factory);

        QSignalSpy success_spy(&mgr, &DriveManager::operationSuccess);
        QSignalSpy error_spy(&mgr, &DriveManager::apiError);

        mgr.deleteItems({ "10" });

        QTRY_COMPARE(error_spy.count(), 1);
        QCOMPARE(success_spy.count(), 0);

        auto arguments = error_spy.takeFirst();
        QCOMPARE(arguments.at(0).toString(), QString("Invalid response format"));
        QCOMPARE(arguments.at(1).toInt(), 0);
        QVERIFY(server.RequestData().contains("DELETE /api/file HTTP/1.1"));
    }

    void DeleteItemsRejectsApiErrorEnvelopeOnHttp200() {
        OneShotHttpServer server(
            QJsonDocument(
                QJsonObject{
                    { "code", 50005 },
                    { "message", "File not found" },
                    { "data", QJsonValue::Null },
                }
            )
                .toJson(QJsonDocument::Compact)
        );
        QVERIFY(server.Listen());

        MockNetworkAccessManager mock_network;
        NetworkClient network_client(static_cast<QNetworkAccessManager*>(&mock_network));
        network_client.SetBaseUrl(server.BaseUrl());
        RequestFactory request_factory;
        request_factory.SetOwnerAccessToken("test_token");
        DriveManager mgr(&network_client, &request_factory);

        QSignalSpy success_spy(&mgr, &DriveManager::operationSuccess);
        QSignalSpy error_spy(&mgr, &DriveManager::apiError);

        mgr.deleteItems({ "10" });

        QTRY_COMPARE(error_spy.count(), 1);
        QCOMPARE(success_spy.count(), 0);

        auto arguments = error_spy.takeFirst();
        QCOMPARE(arguments.at(0).toString(), QString("File not found"));
        QCOMPARE(arguments.at(1).toInt(), 50005);
        QVERIFY(server.RequestData().contains("DELETE /api/file HTTP/1.1"));
    }

    void DeleteItemsSourceBuildsJsonFileIdsBody() {
        QFile source_file(QStringLiteral(QT_TEST_SOURCE_DIR "/../src/managers/DriveManager.cpp"));
        QVERIFY(source_file.open(QIODevice::ReadOnly | QIODevice::Text));

        const QString source = QString::fromUtf8(source_file.readAll());
        QVERIFY(source.contains("body[\"file_ids\"] = ids;"));
        QVERIFY(source.contains("url = url.resolved(QUrl(\"api/file\"));"));
        QVERIFY(!source.contains("base_url + \"api/file\""));
        QVERIFY(source.contains("sendCustomRequest(request, \"DELETE\", json_body)"));
    }

    void ListFilesSendsAuthHeaders() {
        MockNetworkAccessManager mock_network;
        mock_network.RegisterResponse(
            "api/file/list",
            QJsonObject{
                { "code", 0 },
                { "message", "success" },
                { "data",
                  QJsonObject{
                      { "items", QJsonArray{} },
                      { "pagination",
                       QJsonObject{
                           { "page", 1 },
                           { "page_size", 50 },
                           { "total", 0 },
                           { "total_pages", 0 },
                       } },
                  } },
            }
        );

        NetworkClient network_client(static_cast<QNetworkAccessManager*>(&mock_network));
        network_client.SetBaseUrl("http://127.0.0.1:8080/");
        RequestFactory request_factory;
        request_factory.SetOwnerAccessToken("my_access_token");
        DriveManager mgr(&network_client, &request_factory);

        QSignalSpy pagination_spy(&mgr, &DriveManager::paginationLoaded);

        mgr.listFiles("0");

        QTRY_COMPARE(pagination_spy.count(), 1);

        auto request = mock_network.GetRequestLog().constFirst();
        QCOMPARE(
            request.rawHeader("Authorization"),
            QByteArray("Bearer my_access_token")
        );
    }

    void LoadFolderTreePopulatesTreeModel() {
        MockNetworkAccessManager mock_network;
        mock_network.RegisterResponse(
            "api/folder/tree",
            QJsonObject{
                { "code", 0 },
                { "message", "success" },
                { "data",
                  QJsonObject{
                      { "children",
                       QJsonArray{
                           QJsonObject{
                               { "id", 1.0 },
                               { "name", "Documents" },
                               { "children",
                                QJsonArray{
                                    QJsonObject{ { "id", 2.0 }, { "name", "Work" } },
                                } },
                           },
                           QJsonObject{ { "id", 3.0 }, { "name", "Photos" } },
                       } },
                  } },
            }
        );

        NetworkClient network_client(static_cast<QNetworkAccessManager*>(&mock_network));
        network_client.SetBaseUrl("http://127.0.0.1:8080/");
        RequestFactory request_factory;
        request_factory.SetOwnerAccessToken("test_token");
        DriveManager mgr(&network_client, &request_factory);

        QSignalSpy success_spy(&mgr, &DriveManager::operationSuccess);

        mgr.loadFolderTree();

        QTRY_COMPARE(success_spy.count(), 1);

        QCOMPARE(mgr.treeModel()->rowCount(), 2);
        auto idx = mgr.treeModel()->index(0, 0);
        QCOMPARE(mgr.treeModel()->data(idx, FolderTreeModel::NameRole).toString(), QString("Documents"));
        QCOMPARE(mgr.treeModel()->rowCount(idx), 1);
    }
};

int run_TestDriveManager(int argc, char* argv[]) {
    TestDriveManager test;
    return QTest::qExec(&test, argc, argv);
}

#include "test_drive_manager.moc"
