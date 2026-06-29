#include <QJsonObject>
#include <QTest>

#include "models/DownloadTaskModel.hpp"

using namespace disk::desktop;

class TestDownloadTaskModel : public QObject {
    Q_OBJECT

private slots:

    void InitEmpty() {
        DownloadTaskModel model;
        QCOMPARE(model.rowCount(), 0);
    }

    void RoleNamesCount() {
        DownloadTaskModel model;
        auto roles = model.roleNames();
        QCOMPARE(roles.size(), 22);
    }

    void AddTaskIncrementsRowCount() {
        DownloadTaskModel model;

        DownloadTask task;
        task.task_id = "dl_001";
        task.filename = "report.pdf";
        task.file_size = 2097152;
        task.status = "queued";
        model.AddTask(task);

        QCOMPARE(model.rowCount(), 1);
    }

    void DataReturnsCorrectValues() {
        DownloadTaskModel model;

        DownloadTask task;
        task.task_id = "dl_001";
        task.auth_domain = "owner";
        task.file_id = 42;
        task.filename = "report.pdf";
        task.file_size = 2097152;
        task.mime_type = "application/pdf";
        task.target_path = "/home/user/report.pdf";
        task.status = "downloading";
        task.supports_range = true;
        task.transfer_mode = "full";
        task.received_bytes = 1024;
        task.remote_identity = "owner::42:2097152:hash";
        task.expected_size = 2097152;
        task.local_partial_size = 1024;
        task.integrity_hash = "hash";
        task.verification_status = "verified_size_only";
        task.status_detail = "下载完成，已通过大小校验";
        model.AddTask(task);

        auto idx = model.index(0);
        QCOMPARE(model.data(idx, DownloadTaskModel::TaskIdRole).toString(), QString("dl_001"));
        QCOMPARE(model.data(idx, DownloadTaskModel::AuthDomainRole).toString(), QString("owner"));
        QCOMPARE(model.data(idx, DownloadTaskModel::FileIdRole).toULongLong(), quint64(42));
        QCOMPARE(model.data(idx, DownloadTaskModel::FilenameRole).toString(), QString("report.pdf"));
        QCOMPARE(model.data(idx, DownloadTaskModel::FileSizeRole).toULongLong(), quint64(2097152));
        QCOMPARE(model.data(idx, DownloadTaskModel::MimeTypeRole).toString(), QString("application/pdf"));
        QCOMPARE(model.data(idx, DownloadTaskModel::TargetPathRole).toString(), QString("/home/user/report.pdf"));
        QCOMPARE(model.data(idx, DownloadTaskModel::StatusRole).toString(), QString("downloading"));
        QCOMPARE(model.data(idx, DownloadTaskModel::SupportsRangeRole).toBool(), true);
        QCOMPARE(model.data(idx, DownloadTaskModel::TransferModeRole).toString(), QString("full"));
        QCOMPARE(model.data(idx, DownloadTaskModel::ReceivedBytesRole).toULongLong(), quint64(1024));
        QCOMPARE(model.data(idx, DownloadTaskModel::RemoteIdentityRole).toString(), QString("owner::42:2097152:hash"));
        QCOMPARE(model.data(idx, DownloadTaskModel::ExpectedSizeRole).toULongLong(), quint64(2097152));
        QCOMPARE(model.data(idx, DownloadTaskModel::LocalPartialSizeRole).toULongLong(), quint64(1024));
        QCOMPARE(model.data(idx, DownloadTaskModel::IntegrityHashRole).toString(), QString("hash"));
        QCOMPARE(model.data(idx, DownloadTaskModel::VerificationStatusRole).toString(), QString("verified_size_only"));
        QCOMPARE(model.data(idx, DownloadTaskModel::StatusDetailRole).toString(), QString("下载完成，已通过大小校验"));
    }

    void VisitorTaskWithShareId() {
        DownloadTaskModel model;

        DownloadTask task;
        task.task_id = "dl_visitor";
        task.auth_domain = "visitor";
        task.share_id = "sh_abc";
        task.file_id = 100;
        task.filename = "shared_doc.txt";
        task.status = "queued";
        model.AddTask(task);

        auto idx = model.index(0);
        QCOMPARE(model.data(idx, DownloadTaskModel::AuthDomainRole).toString(), QString("visitor"));
        QCOMPARE(model.data(idx, DownloadTaskModel::ShareIdRole).toString(), QString("sh_abc"));
    }

    void OptionalFieldsReturnInvalidWhenEmpty() {
        DownloadTaskModel model;

        DownloadTask task;
        task.task_id = "dl_min";
        task.filename = "min.txt";
        model.AddTask(task);

        auto idx = model.index(0);
        QCOMPARE(model.data(idx, DownloadTaskModel::ShareIdRole), QVariant());
        QCOMPARE(model.data(idx, DownloadTaskModel::FileHashRole), QVariant());
        QCOMPARE(model.data(idx, DownloadTaskModel::RangeStartRole), QVariant());
        QCOMPARE(model.data(idx, DownloadTaskModel::RangeEndRole), QVariant());
        QCOMPARE(model.data(idx, DownloadTaskModel::ErrorRole), QVariant());
    }

    void RangeFieldsWhenSet() {
        DownloadTaskModel model;

        DownloadTask task;
        task.task_id = "dl_range";
        task.filename = "resume.bin";
        task.transfer_mode = "range";
        task.range_start = quint64(1024);
        task.range_end = quint64(4096);
        model.AddTask(task);

        auto idx = model.index(0);
        QCOMPARE(model.data(idx, DownloadTaskModel::RangeStartRole).toULongLong(), quint64(1024));
        QCOMPARE(model.data(idx, DownloadTaskModel::RangeEndRole).toULongLong(), quint64(4096));
    }

    void ErrorRoleReturnsJsonObject() {
        DownloadTaskModel model;

        DownloadTask task;
        task.task_id = "dl_err";
        task.filename = "error.bin";
        task.status = "failed";
        ApiError err;
        err.code = 40108;
        err.message = "Session expired";
        err.category = "SessionExpired";
        task.error = err;
        model.AddTask(task);

        auto idx = model.index(0);
        auto err_var = model.data(idx, DownloadTaskModel::ErrorRole);
        QVERIFY(err_var.isValid());
        auto err_obj = err_var.toJsonObject();
        QCOMPARE(err_obj["code"].toInt(), 40108);
        QCOMPARE(err_obj["message"].toString(), QString("Session expired"));
    }

    void RemoveTask() {
        DownloadTaskModel model;

        DownloadTask a;
        a.task_id = "a";
        a.filename = "a.txt";
        DownloadTask b;
        b.task_id = "b";
        b.filename = "b.txt";
        model.AddTask(a);
        model.AddTask(b);

        QVERIFY(model.RemoveTask("a"));
        QCOMPARE(model.rowCount(), 1);
        QCOMPARE(model.data(model.index(0), DownloadTaskModel::TaskIdRole).toString(), QString("b"));
    }

    void RemoveNonexistentReturnsFalse() {
        DownloadTaskModel model;
        QVERIFY(!model.RemoveTask("nope"));
    }

    void UpdateTask() {
        DownloadTaskModel model;

        DownloadTask task;
        task.task_id = "dl_upd";
        task.filename = "orig.pdf";
        task.status = "queued";
        task.received_bytes = 0;
        model.AddTask(task);

        DownloadTask updated;
        updated.task_id = "dl_upd";
        updated.filename = "orig.pdf";
        updated.status = "completed";
        updated.received_bytes = 4096;
        QVERIFY(model.UpdateTask("dl_upd", updated));

        auto idx = model.index(0);
        QCOMPARE(model.data(idx, DownloadTaskModel::StatusRole).toString(), QString("completed"));
        QCOMPARE(model.data(idx, DownloadTaskModel::ReceivedBytesRole).toULongLong(), quint64(4096));
    }

    void ClearEmptiesModel() {
        DownloadTaskModel model;
        DownloadTask a;
        a.task_id = "x";
        DownloadTask b;
        b.task_id = "y";
        model.AddTask(a);
        model.AddTask(b);
        QCOMPARE(model.rowCount(), 2);

        model.Clear();
        QCOMPARE(model.rowCount(), 0);
    }

    void FindTaskReturnsCorrectIndex() {
        DownloadTaskModel model;
        DownloadTask a, b, c;
        a.task_id = "d1";
        b.task_id = "d2";
        c.task_id = "d3";
        model.AddTask(a);
        model.AddTask(b);
        model.AddTask(c);

        QCOMPARE(model.FindTask("d1"), 0);
        QCOMPARE(model.FindTask("d2"), 1);
        QCOMPARE(model.FindTask("d3"), 2);
        QCOMPARE(model.FindTask("missing"), -1);
    }

    void GetTaskReturnsCorrectItem() {
        DownloadTaskModel model;
        DownloadTask task;
        task.task_id = "get_me";
        task.filename = "target.zip";
        model.AddTask(task);

        auto retrieved = model.GetTask(0);
        QVERIFY(retrieved.has_value());
        QCOMPARE(retrieved->task_id, QString("get_me"));
        QCOMPARE(retrieved->filename, QString("target.zip"));

        auto invalid = model.GetTask(99);
        QVERIFY(!invalid.has_value());
    }

    void FromJsonParsesAllFields() {
        QJsonObject json;
        json["task_id"] = "json_dl";
        json["auth_domain"] = "owner";
        json["share_id"] = "sh_test";
        json["file_id"] = 55.0;
        json["filename"] = "data.csv";
        json["file_size"] = 8192.0;
        json["file_hash"] = "hashabc";
        json["mime_type"] = "text/csv";
        json["supports_range"] = true;
        json["transfer_mode"] = "range";
        json["range_start"] = 1024.0;
        json["range_end"] = 8192.0;
        json["target_path"] = "/tmp/data.csv";
        json["received_bytes"] = 1024.0;
        json["remote_identity"] = "owner:sh_test:55:8192:hashabc";
        json["expected_size"] = 8192.0;
        json["local_partial_size"] = 1024.0;
        json["integrity_hash"] = "hashabc";
        json["verification_status"] = "verified_hash";
        json["status_detail"] = "从上次中断处继续下载";
        json["status"] = "paused";

        auto task = DownloadTask::FromJson(json);
        QCOMPARE(task.task_id, QString("json_dl"));
        QCOMPARE(task.auth_domain, QString("owner"));
        QVERIFY(task.share_id.has_value());
        QCOMPARE(*task.share_id, QString("sh_test"));
        QCOMPARE(task.file_id, quint64(55));
        QCOMPARE(task.filename, QString("data.csv"));
        QCOMPARE(task.file_size, quint64(8192));
        QVERIFY(task.file_hash.has_value());
        QCOMPARE(*task.file_hash, QString("hashabc"));
        QCOMPARE(task.mime_type, QString("text/csv"));
        QVERIFY(task.supports_range);
        QCOMPARE(task.transfer_mode, QString("range"));
        QVERIFY(task.range_start.has_value());
        QCOMPARE(*task.range_start, quint64(1024));
        QVERIFY(task.range_end.has_value());
        QCOMPARE(*task.range_end, quint64(8192));
        QCOMPARE(task.target_path, QString("/tmp/data.csv"));
        QCOMPARE(task.received_bytes, quint64(1024));
        QCOMPARE(task.remote_identity, QString("owner:sh_test:55:8192:hashabc"));
        QCOMPARE(task.expected_size, quint64(8192));
        QCOMPARE(task.local_partial_size, quint64(1024));
        QVERIFY(task.integrity_hash.has_value());
        QCOMPARE(*task.integrity_hash, QString("hashabc"));
        QCOMPARE(task.verification_status, QString("verified_hash"));
        QCOMPARE(task.status_detail, QString("从上次中断处继续下载"));
        QCOMPARE(task.status, QString("paused"));
    }

    void ToJsonRoundTrip() {
        DownloadTask task;
        task.task_id = "rt_dl";
        task.auth_domain = "visitor";
        task.share_id = "sh_rt";
        task.file_id = 10;
        task.filename = "file.zip";
        task.file_size = 65536;
        task.file_hash = "abcdef";
        task.mime_type = "application/zip";
        task.supports_range = true;
        task.transfer_mode = "range";
        task.range_start = quint64(1000);
        task.range_end = quint64(65536);
        task.target_path = "/out/file.zip";
        task.received_bytes = 1000;
        task.remote_identity = "visitor:sh_rt:10:65536:abcdef";
        task.expected_size = 65536;
        task.local_partial_size = 1000;
        task.integrity_hash = "abcdef";
        task.verification_status = "verified_hash";
        task.status_detail = "从上次中断处继续下载";
        task.status = "downloading";

        auto json = task.ToJson();
        QCOMPARE(json["task_id"].toString(), QString("rt_dl"));
        QCOMPARE(json["auth_domain"].toString(), QString("visitor"));
        QCOMPARE(json["file_id"].toDouble(), 10.0);
        QCOMPARE(json["filename"].toString(), QString("file.zip"));
        QCOMPARE(json["status"].toString(), QString("downloading"));
        QVERIFY(json.contains("share_id"));
        QVERIFY(json.contains("range_start"));
        QCOMPARE(json["remote_identity"].toString(), QString("visitor:sh_rt:10:65536:abcdef"));
        QCOMPARE(json["expected_size"].toDouble(), 65536.0);
        QCOMPARE(json["local_partial_size"].toDouble(), 1000.0);
        QCOMPARE(json["integrity_hash"].toString(), QString("abcdef"));
        QCOMPARE(json["verification_status"].toString(), QString("verified_hash"));
        QCOMPARE(json["status_detail"].toString(), QString("从上次中断处继续下载"));
    }
};

int run_TestDownloadTaskModel(int argc, char* argv[]) {
    TestDownloadTaskModel test;
    return QTest::qExec(&test, argc, argv);
}

#include "test_download_task_model.moc"
