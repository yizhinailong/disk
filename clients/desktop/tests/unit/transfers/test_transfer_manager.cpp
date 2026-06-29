#include <QJsonDocument>
#include <QSignalSpy>
#include <QTest>

#include "helpers/MockNetworkAccessManager.hpp"
#include "managers/TransferManager.hpp"
#include "network/NetworkClient.hpp"
#include "network/RequestFactory.hpp"

#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTemporaryFile>

using namespace disk::desktop;
using namespace disk::desktop::managers;
using namespace disk::desktop::testing;

class TestTransferManager : public QObject {
    Q_OBJECT

private:
    static auto Md5(const QByteArray& data) -> QString {
        return QString(QCryptographicHash::hash(data, QCryptographicHash::Md5).toHex());
    }

    static void WriteResumeState(
        const QString& target_path,
        const QString& remote_identity,
        quint64 expected_size,
        quint64 local_partial_size,
        const QString& integrity_hash = {}
    ) {
        QFile state_file(target_path + QStringLiteral(".download.json"));
        QVERIFY(state_file.open(QIODevice::WriteOnly | QIODevice::Truncate));

        QJsonObject json;
        json["remote_identity"] = remote_identity;
        json["expected_size"] = static_cast<double>(expected_size);
        json["local_partial_size"] = static_cast<double>(local_partial_size);
        json["integrity_hash"] = integrity_hash;
        state_file.write(QJsonDocument(json).toJson(QJsonDocument::Compact));
    }

private slots:

    void InitHasEmptyModels() {
        NetworkClient nc;
        RequestFactory rf;
        TransferManager mgr(&nc, &rf);

        QVERIFY(mgr.GetUploadModel() != nullptr);
        QVERIFY(mgr.GetDownloadModel() != nullptr);
        QCOMPARE(mgr.GetUploadModel()->rowCount(), 0);
        QCOMPARE(mgr.GetDownloadModel()->rowCount(), 0);
        QCOMPARE(mgr.GetLocalReservedBytes(), quint64(0));
    }

    void ClearCompletedUploadsRemovesCompleted() {
        NetworkClient nc;
        RequestFactory rf;
        TransferManager mgr(&nc, &rf);

        UploadTask a;
        a.task_id = "up_done";
        a.filename = "done.txt";
        a.status = "completed";
        mgr.GetUploadModel()->AddTask(a);

        UploadTask b;
        b.task_id = "up_active";
        b.filename = "active.txt";
        b.status = "uploading";
        mgr.GetUploadModel()->AddTask(b);

        QCOMPARE(mgr.GetUploadModel()->rowCount(), 2);

        mgr.ClearCompletedUploads();

        QCOMPARE(mgr.GetUploadModel()->rowCount(), 1);
        QCOMPARE(
            mgr.GetUploadModel()->data(mgr.GetUploadModel()->index(0), UploadTaskModel::StatusRole).toString(),
            QString("uploading")
        );
    }

    void ClearCompletedDownloadsRemovesCompleted() {
        NetworkClient nc;
        RequestFactory rf;
        TransferManager mgr(&nc, &rf);

        DownloadTask a;
        a.task_id = "dl_done";
        a.filename = "done.pdf";
        a.status = "completed";
        mgr.GetDownloadModel()->AddTask(a);

        DownloadTask b;
        b.task_id = "dl_fail";
        b.filename = "failed.pdf";
        b.status = "failed";
        mgr.GetDownloadModel()->AddTask(b);

        QCOMPARE(mgr.GetDownloadModel()->rowCount(), 2);

        mgr.ClearCompletedDownloads();

        QCOMPARE(mgr.GetDownloadModel()->rowCount(), 1);
        QCOMPARE(
            mgr.GetDownloadModel()->data(mgr.GetDownloadModel()->index(0), DownloadTaskModel::StatusRole).toString(),
            QString("failed")
        );
    }

    void ClearCompletedUploadsAlsoRemovesCancelled() {
        NetworkClient nc;
        RequestFactory rf;
        TransferManager mgr(&nc, &rf);

        UploadTask a;
        a.task_id = "up_cancel";
        a.filename = "cancel.txt";
        a.status = "cancelled";
        mgr.GetUploadModel()->AddTask(a);

        mgr.ClearCompletedUploads();
        QCOMPARE(mgr.GetUploadModel()->rowCount(), 0);
    }

    void ClearCompletedDownloadsAlsoRemovesCancelled() {
        NetworkClient nc;
        RequestFactory rf;
        TransferManager mgr(&nc, &rf);

        DownloadTask a;
        a.task_id = "dl_cancel";
        a.filename = "cancel.pdf";
        a.status = "cancelled";
        mgr.GetDownloadModel()->AddTask(a);

        mgr.ClearCompletedDownloads();
        QCOMPARE(mgr.GetDownloadModel()->rowCount(), 0);
    }

    void LocalReservedBytesCountsActiveUploads() {
        NetworkClient nc;
        RequestFactory rf;
        TransferManager mgr(&nc, &rf);

        UploadTask active;
        active.task_id = "active";
        active.filename = "big.bin";
        active.file_size = 10485760;
        active.status = "uploading";
        mgr.GetUploadModel()->AddTask(active);

        UploadTask queued;
        queued.task_id = "queued";
        queued.filename = "small.txt";
        queued.file_size = 100;
        queued.status = "queued";
        mgr.GetUploadModel()->AddTask(queued);

        QCOMPARE(mgr.GetLocalReservedBytes(), quint64(10485760));
    }

    void LocalReservedBytesCountsInitializingAndCompleting() {
        NetworkClient nc;
        RequestFactory rf;
        TransferManager mgr(&nc, &rf);

        UploadTask init;
        init.task_id = "init";
        init.filename = "a.bin";
        init.file_size = 5000;
        init.status = "initializing";
        mgr.GetUploadModel()->AddTask(init);

        UploadTask comp;
        comp.task_id = "comp";
        comp.filename = "b.bin";
        comp.file_size = 3000;
        comp.status = "completing";
        mgr.GetUploadModel()->AddTask(comp);

        QCOMPARE(mgr.GetLocalReservedBytes(), quint64(8000));
    }

    void UploadStateStringMapping() {
        QCOMPARE(ToString(UploadState::Queued), QString("queued"));
        QCOMPARE(ToString(UploadState::Hashing), QString("hashing"));
        QCOMPARE(ToString(UploadState::Initializing), QString("initializing"));
        QCOMPARE(ToString(UploadState::Uploading), QString("uploading"));
        QCOMPARE(ToString(UploadState::Completed), QString("completed"));
        QCOMPARE(ToString(UploadState::Cancelled), QString("cancelled"));
        QCOMPARE(ToString(UploadState::Failed), QString("failed"));
        QCOMPARE(ToString(UploadState::Expired), QString("expired"));
        QCOMPARE(ToString(UploadState::InstantUploaded), QString("completed"));
        QCOMPARE(ToString(UploadState::Resuming), QString("uploading"));
        QCOMPARE(ToString(UploadState::Completing), QString("completing"));
        QCOMPARE(ToString(UploadState::CancelPending), QString("cancelling"));
    }

    void DownloadStateStringMapping() {
        QCOMPARE(ToString(DownloadState::Idle), QString("idle"));
        QCOMPARE(ToString(DownloadState::FetchingMetadata), QString("fetching_metadata"));
        QCOMPARE(ToString(DownloadState::Ready), QString("ready"));
        QCOMPARE(ToString(DownloadState::TransferringFull), QString("downloading"));
        QCOMPARE(ToString(DownloadState::TransferringRange), QString("downloading"));
        QCOMPARE(ToString(DownloadState::Paused), QString("paused"));
        QCOMPARE(ToString(DownloadState::Completed), QString("completed"));
        QCOMPARE(ToString(DownloadState::Cancelled), QString("cancelled"));
        QCOMPARE(ToString(DownloadState::Failed), QString("failed"));
        QCOMPARE(ToString(DownloadState::RetryWaiting), QString("retry_waiting"));
    }

    void StartUploadCreatesTaskForExistingFile() {
        QTemporaryFile tmp;
        QVERIFY(tmp.open());
        tmp.write("hello world");
        tmp.close();

        MockNetworkAccessManager mock_network;
        NetworkClient nc(static_cast<QNetworkAccessManager*>(&mock_network));
        RequestFactory rf;
        TransferManager mgr(&nc, &rf);

        mgr.StartUpload(tmp.fileName(), 0);

        QCOMPARE(mgr.GetUploadModel()->rowCount(), 1);
        auto task = mgr.GetUploadModel()->GetTask(0);
        QVERIFY(task.has_value());
        QCOMPARE(task->status, QString("hashing"));
    }

    void StartUploadSkipsNonexistentFile() {
        NetworkClient nc;
        RequestFactory rf;
        TransferManager mgr(&nc, &rf);

        mgr.StartUpload("/nonexistent/path/file.txt", 0);

        QCOMPARE(mgr.GetUploadModel()->rowCount(), 0);
    }

    void StartDownloadCreatesQueuedTask() {
        MockNetworkAccessManager mock_network;
        mock_network.RegisterResponse(
            "api/file/download/42/info",
            QJsonObject{
                { "code", 0 },
                { "message", "success" },
                { "data",
                  QJsonObject{
                      { "filename", "report.pdf" },
                      { "file_size", 1048576.0 },
                      { "file_hash", "abc" },
                      { "mime_type", "application/pdf" },
                      { "supports_range", false },
                  } },
            }
        );

        NetworkClient nc(static_cast<QNetworkAccessManager*>(&mock_network));
        nc.SetBaseUrl("http://127.0.0.1:8080/");
        RequestFactory rf;
        rf.SetOwnerAccessToken("test_token");
        TransferManager mgr(&nc, &rf);

        mgr.StartDownload(42, "/tmp/report.pdf");

        QCOMPARE(mgr.GetDownloadModel()->rowCount(), 1);
        auto task = mgr.GetDownloadModel()->GetTask(0);
        QVERIFY(task.has_value());
        QCOMPARE(task->file_id, quint64(42));
        QCOMPARE(task->target_path, QString("/tmp/report.pdf"));
        QCOMPARE(task->auth_domain, QString("owner"));
    }

    void StartUploadEmitsCompletionForChunkedUpload() {
        QTemporaryFile tmp;
        QVERIFY(tmp.open());
        tmp.write("hello upload");
        tmp.close();

        MockNetworkAccessManager mock_network;
        mock_network.RegisterResponse(
            "api/file/upload/init",
            QJsonObject{
                { "code", 0 },
                { "message", "success" },
                { "data",
                  QJsonObject{
                      { "upload_id", "upload_abc" },
                      { "chunk_size", 1024 },
                      { "total_chunks", 1 },
                      { "instant_upload", false },
                  } },
            }
        );
        mock_network.RegisterResponse(
            "api/file/upload/chunk",
            QJsonObject{
                { "code", 0 },
                { "message", "success" },
                { "data", QJsonObject{ { "chunk_index", 0 }, { "uploaded", true } } },
            }
        );
        mock_network.RegisterResponse(
            "api/file/upload/complete",
            QJsonObject{
                { "code", 0 },
                { "message", "success" },
                { "data", QJsonObject{ { "file", QJsonObject{ { "id", 1 } } } } },
            }
        );

        NetworkClient nc(static_cast<QNetworkAccessManager*>(&mock_network));
        RequestFactory rf;
        TransferManager mgr(&nc, &rf);
        QSignalSpy completed_spy(&mgr, &TransferManager::uploadCompleted);

        mgr.StartUpload(tmp.fileName(), 7);

        QTRY_COMPARE(completed_spy.count(), 1);
        auto task = mgr.GetUploadModel()->GetTask(0);
        QVERIFY(task.has_value());
        QCOMPARE(task->status, QString("completed"));
        QCOMPARE(completed_spy.at(0).at(1).toString(), task->filename);
        QCOMPARE(completed_spy.at(0).at(2).toULongLong(), quint64(7));
    }

    void StartUploadEmitsCompletionForInstantUpload() {
        QTemporaryFile tmp;
        QVERIFY(tmp.open());
        tmp.write("hello instant");
        tmp.close();

        MockNetworkAccessManager mock_network;
        mock_network.RegisterResponse(
            "api/file/upload/init",
            QJsonObject{
                { "code", 0 },
                { "message", "success" },
                { "data",
                  QJsonObject{
                      { "upload_id", "" },
                      { "chunk_size", 0 },
                      { "total_chunks", 0 },
                      { "instant_upload", true },
                      { "file", QJsonObject{ { "id", 1 } } },
                  } },
            }
        );

        NetworkClient nc(static_cast<QNetworkAccessManager*>(&mock_network));
        RequestFactory rf;
        TransferManager mgr(&nc, &rf);
        QSignalSpy completed_spy(&mgr, &TransferManager::uploadCompleted);

        mgr.StartUpload(tmp.fileName(), 9);

        QTRY_COMPARE(completed_spy.count(), 1);
        auto task = mgr.GetUploadModel()->GetTask(0);
        QVERIFY(task.has_value());
        QCOMPARE(task->status, QString("completed"));
        QVERIFY(task->instant_upload);
        QCOMPARE(completed_spy.at(0).at(1).toString(), task->filename);
        QCOMPARE(completed_spy.at(0).at(2).toULongLong(), quint64(9));
    }

    void ShutdownOwnerTransfersCancelsNonTerminalOwnerTransfers() {
        NetworkClient nc;
        RequestFactory rf;
        TransferManager mgr(&nc, &rf);

        UploadTask active_upload;
        active_upload.task_id = "up_active";
        active_upload.status = "initializing";
        mgr.GetUploadModel()->AddTask(active_upload);

        UploadTask completed_upload;
        completed_upload.task_id = "up_done";
        completed_upload.status = "completed";
        mgr.GetUploadModel()->AddTask(completed_upload);

        DownloadTask owner_download;
        owner_download.task_id = "dl_owner";
        owner_download.auth_domain = "owner";
        owner_download.status = "paused";
        mgr.GetDownloadModel()->AddTask(owner_download);

        DownloadTask visitor_download;
        visitor_download.task_id = "dl_visitor";
        visitor_download.auth_domain = "visitor";
        visitor_download.status = "paused";
        mgr.GetDownloadModel()->AddTask(visitor_download);

        mgr.ShutdownOwnerTransfers();

        auto cancelled_upload = mgr.GetUploadModel()->GetTask(mgr.GetUploadModel()->FindTask("up_active"));
        QVERIFY(cancelled_upload.has_value());
        QCOMPARE(cancelled_upload->status, QString("cancelled"));

        auto preserved_completed_upload = mgr.GetUploadModel()->GetTask(mgr.GetUploadModel()->FindTask("up_done"));
        QVERIFY(preserved_completed_upload.has_value());
        QCOMPARE(preserved_completed_upload->status, QString("completed"));

        auto cancelled_owner_download = mgr.GetDownloadModel()->GetTask(mgr.GetDownloadModel()->FindTask("dl_owner"));
        QVERIFY(cancelled_owner_download.has_value());
        QCOMPARE(cancelled_owner_download->status, QString("cancelled"));

        auto preserved_visitor_download = mgr.GetDownloadModel()->GetTask(mgr.GetDownloadModel()->FindTask("dl_visitor"));
        QVERIFY(preserved_visitor_download.has_value());
        QCOMPARE(preserved_visitor_download->status, QString("paused"));
    }

    void VisitorShareDownloadResumesWithTrustedPartialState() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString target_path = dir.filePath("shared.bin");
        QFile partial(target_path);
        QVERIFY(partial.open(QIODevice::WriteOnly));
        partial.write("abc");
        partial.close();

        const QString hash = Md5("abcdef");
        WriteResumeState(target_path, QStringLiteral("visitor:share-1:77:6:%1").arg(hash), 6, 3, hash);

        MockNetworkAccessManager mock_network;
        mock_network.RegisterRawResponse(
            "api/share/download/share-1/77",
            QByteArray("def"),
            206
        );

        NetworkClient nc(static_cast<QNetworkAccessManager*>(&mock_network));
        nc.SetBaseUrl("http://127.0.0.1:8080/");
        RequestFactory rf;
        rf.SetVisitorShareToken("share_token");
        TransferManager mgr(&nc, &rf);

        mgr.StartShareDownload("share-1", 77, target_path, "shared.bin", 6, hash);

        QTRY_VERIFY(mock_network.GetRequestLog().size() >= 1);
        const auto transfer_request = mock_network.GetRequestLog().at(0);
        QCOMPARE(transfer_request.rawHeader("X-Share-Token"), QByteArray("share_token"));
        QCOMPARE(transfer_request.rawHeader("Range"), QByteArray("bytes=3-"));

        QTRY_COMPARE(mgr.GetDownloadModel()->GetTask(0)->status, QString("completed"));
        const auto task = mgr.GetDownloadModel()->GetTask(0);
        QVERIFY(task.has_value());
        QCOMPARE(task->transfer_mode, QString("range"));
        QCOMPARE(task->received_bytes, quint64(6));
        QCOMPARE(task->local_partial_size, quint64(6));
        QCOMPARE(task->verification_status, QString("verified_hash"));
        QVERIFY(task->status_detail.contains(QStringLiteral("从上次中断处继续下载")));
        QCOMPARE(QFileInfo(target_path + QStringLiteral(".download.json")).exists(), false);
    }

    void VisitorShareDownloadRestartsWhenPartialStateIsMissing() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString target_path = dir.filePath("shared.bin");
        QFile partial(target_path);
        QVERIFY(partial.open(QIODevice::WriteOnly));
        partial.write("abc");
        partial.close();

        MockNetworkAccessManager mock_network;
        mock_network.RegisterRawResponse(
            "api/share/download/share-2/78",
            QByteArray("abcdef")
        );

        NetworkClient nc(static_cast<QNetworkAccessManager*>(&mock_network));
        nc.SetBaseUrl("http://127.0.0.1:8080/");
        RequestFactory rf;
        rf.SetVisitorShareToken("share_token");
        TransferManager mgr(&nc, &rf);

        mgr.StartShareDownload("share-2", 78, target_path, "shared.bin", 6);

        QTRY_VERIFY(mock_network.GetRequestLog().size() >= 1);
        const auto transfer_request = mock_network.GetRequestLog().at(0);
        QVERIFY(transfer_request.rawHeader("Range").isEmpty());

        QTRY_COMPARE(mgr.GetDownloadModel()->GetTask(0)->status, QString("completed"));
        const auto task = mgr.GetDownloadModel()->GetTask(0);
        QVERIFY(task.has_value());
        QCOMPARE(task->transfer_mode, QString("full"));
        QCOMPARE(QFileInfo(target_path).size(), qint64(6));
        QVERIFY(task->status_detail.contains(QStringLiteral("缺少可信的断点续传记录")));
        QCOMPARE(task->verification_status, QString("verified_size_only"));
    }

    void VisitorShareDownloadRestartsWhenRangeIsRejected() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString target_path = dir.filePath("shared.bin");
        QFile partial(target_path);
        QVERIFY(partial.open(QIODevice::WriteOnly));
        partial.write("abc");
        partial.close();

        const QString hash = Md5("abcdef");
        WriteResumeState(target_path, QStringLiteral("visitor:share-3:79:6:%1").arg(hash), 6, 3, hash);

        MockNetworkAccessManager mock_network;
        mock_network.RegisterRawResponse(
            "api/share/download/share-3/79",
            QByteArray("abcdef")
        );

        NetworkClient nc(static_cast<QNetworkAccessManager*>(&mock_network));
        nc.SetBaseUrl("http://127.0.0.1:8080/");
        RequestFactory rf;
        rf.SetVisitorShareToken("share_token");
        TransferManager mgr(&nc, &rf);

        mgr.StartShareDownload("share-3", 79, target_path, "shared.bin", 6, hash);

        QTRY_VERIFY(mock_network.GetRequestLog().size() >= 2);
        QCOMPARE(mock_network.GetRequestLog().at(0).rawHeader("Range"), QByteArray("bytes=3-"));
        QVERIFY(mock_network.GetRequestLog().at(1).rawHeader("Range").isEmpty());

        QTRY_COMPARE(mgr.GetDownloadModel()->GetTask(0)->status, QString("completed"));
        const auto task = mgr.GetDownloadModel()->GetTask(0);
        QVERIFY(task.has_value());
        QCOMPARE(task->transfer_mode, QString("full"));
        QVERIFY(task->status_detail.contains(QStringLiteral("服务器拒绝断点续传")));
    }

    void VisitorShareDownloadFailsOnSizeVerificationMismatch() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString target_path = dir.filePath("shared.bin");

        MockNetworkAccessManager mock_network;
        mock_network.RegisterRawResponse(
            "api/share/download/share-4/80",
            QByteArray("abc")
        );

        NetworkClient nc(static_cast<QNetworkAccessManager*>(&mock_network));
        nc.SetBaseUrl("http://127.0.0.1:8080/");
        RequestFactory rf;
        rf.SetVisitorShareToken("share_token");
        TransferManager mgr(&nc, &rf);
        QSignalSpy error_spy(&mgr, &TransferManager::taskError);

        mgr.StartShareDownload("share-4", 80, target_path, "shared.bin", 6);

        QTRY_COMPARE(mgr.GetDownloadModel()->GetTask(0)->status, QString("failed"));
        const auto task = mgr.GetDownloadModel()->GetTask(0);
        QVERIFY(task.has_value());
        QVERIFY(task->error.has_value());
        QCOMPARE(task->error->category, QString("VerificationFailed"));
        QCOMPARE(task->verification_status, QString("failed"));
        QVERIFY(task->status_detail.contains(QStringLiteral("文件大小不匹配")));
        QCOMPARE(error_spy.count(), 1);
    }

    void VisitorShareDownloadFailsOnHashVerificationMismatch() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString target_path = dir.filePath("shared.bin");

        MockNetworkAccessManager mock_network;
        mock_network.RegisterRawResponse(
            "api/share/download/share-5/81",
            QByteArray("abc")
        );

        NetworkClient nc(static_cast<QNetworkAccessManager*>(&mock_network));
        nc.SetBaseUrl("http://127.0.0.1:8080/");
        RequestFactory rf;
        rf.SetVisitorShareToken("share_token");
        TransferManager mgr(&nc, &rf);
        QSignalSpy error_spy(&mgr, &TransferManager::taskError);

        mgr.StartShareDownload("share-5", 81, target_path, "shared.bin", 3, QStringLiteral("00000000000000000000000000000000"));

        QTRY_COMPARE(mgr.GetDownloadModel()->GetTask(0)->status, QString("failed"));
        const auto task = mgr.GetDownloadModel()->GetTask(0);
        QVERIFY(task.has_value());
        QVERIFY(task->error.has_value());
        QCOMPARE(task->error->category, QString("VerificationFailed"));
        QCOMPARE(task->verification_status, QString("failed"));
        QVERIFY(task->status_detail.contains(QStringLiteral("文件哈希不匹配")));
        QCOMPARE(error_spy.count(), 1);
    }

    void DownloadFailsWhenCompletedSizeMismatches() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString target_path = dir.filePath("report.pdf");

        MockNetworkAccessManager mock_network;
        mock_network.RegisterResponse(
            "api/file/download/91/info",
            QJsonObject{
                { "code", 0 },
                { "message", "success" },
                { "data",
                  QJsonObject{
                      { "filename", "report.pdf" },
                      { "file_size", 6.0 },
                      { "file_hash", "" },
                      { "mime_type", "application/pdf" },
                      { "supports_range", false },
                  } },
            }
        );
        mock_network.RegisterRawResponse("api/file/download/91", QByteArray("abc"));

        NetworkClient nc(static_cast<QNetworkAccessManager*>(&mock_network));
        nc.SetBaseUrl("http://127.0.0.1:8080/");
        RequestFactory rf;
        rf.SetOwnerAccessToken("owner_token");
        TransferManager mgr(&nc, &rf);
        QSignalSpy error_spy(&mgr, &TransferManager::taskError);

        mgr.StartDownload(91, target_path);

        QTRY_COMPARE(mgr.GetDownloadModel()->GetTask(0)->status, QString("failed"));
        QCOMPARE(mgr.GetDownloadModel()->GetTask(0)->error->category, QString("IntegrityError"));
        QCOMPARE(error_spy.count(), 1);
    }

    void DownloadFailsWhenCompletedHashMismatches() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString target_path = dir.filePath("report.pdf");

        MockNetworkAccessManager mock_network;
        mock_network.RegisterResponse(
            "api/file/download/92/info",
            QJsonObject{
                { "code", 0 },
                { "message", "success" },
                { "data",
                  QJsonObject{
                      { "filename", "report.pdf" },
                      { "file_size", 3.0 },
                      { "file_hash", QString("00000000000000000000000000000000") },
                      { "mime_type", "application/pdf" },
                      { "supports_range", false },
                  } },
            }
        );
        mock_network.RegisterRawResponse("api/file/download/92", QByteArray("abc"));

        NetworkClient nc(static_cast<QNetworkAccessManager*>(&mock_network));
        nc.SetBaseUrl("http://127.0.0.1:8080/");
        RequestFactory rf;
        rf.SetOwnerAccessToken("owner_token");
        TransferManager mgr(&nc, &rf);
        QSignalSpy error_spy(&mgr, &TransferManager::taskError);

        mgr.StartDownload(92, target_path);

        QTRY_COMPARE(mgr.GetDownloadModel()->GetTask(0)->status, QString("failed"));
        QCOMPARE(mgr.GetDownloadModel()->GetTask(0)->error->category, QString("IntegrityError"));
        QCOMPARE(error_spy.count(), 1);
    }

    void DownloadCompletesWithSizeOnlyWhenHashIsMissing() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString target_path = dir.filePath("report.pdf");

        MockNetworkAccessManager mock_network;
        mock_network.RegisterResponse(
            "api/file/download/93/info",
            QJsonObject{
                { "code", 0 },
                { "message", "success" },
                { "data",
                  QJsonObject{
                      { "filename", "report.pdf" },
                      { "file_size", 3.0 },
                      { "file_hash", "" },
                      { "mime_type", "application/pdf" },
                      { "supports_range", false },
                  } },
            }
        );
        mock_network.RegisterRawResponse("api/file/download/93", QByteArray("abc"));

        NetworkClient nc(static_cast<QNetworkAccessManager*>(&mock_network));
        nc.SetBaseUrl("http://127.0.0.1:8080/");
        RequestFactory rf;
        rf.SetOwnerAccessToken("owner_token");
        TransferManager mgr(&nc, &rf);

        mgr.StartDownload(93, target_path);

        QTRY_COMPARE(mgr.GetDownloadModel()->GetTask(0)->status, QString("completed"));
        QCOMPARE(mgr.GetDownloadModel()->GetTask(0)->received_bytes, quint64(3));
    }

};

int run_TestTransferManager(int argc, char* argv[]) {
    TestTransferManager test;
    return QTest::qExec(&test, argc, argv);
}

#include "test_transfer_manager.moc"
