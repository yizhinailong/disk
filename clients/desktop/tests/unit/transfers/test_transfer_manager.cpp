#include <QSignalSpy>
#include <QTest>

#include "helpers/MockNetworkAccessManager.hpp"
#include "managers/TransferManager.hpp"
#include "network/NetworkClient.hpp"
#include "network/RequestFactory.hpp"

#include <QTemporaryFile>

using namespace disk::desktop;
using namespace disk::desktop::managers;
using namespace disk::desktop::testing;

class TestTransferManager : public QObject {
    Q_OBJECT

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
};

int run_TestTransferManager(int argc, char* argv[]) {
    TestTransferManager test;
    return QTest::qExec(&test, argc, argv);
}

#include "test_transfer_manager.moc"
