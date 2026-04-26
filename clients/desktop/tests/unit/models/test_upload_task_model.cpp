#include <QJsonArray>
#include <QJsonObject>
#include <QTest>

#include "models/UploadTaskModel.hpp"

using namespace disk::desktop;

class TestUploadTaskModel : public QObject {
    Q_OBJECT

private slots:

    void InitEmpty() {
        UploadTaskModel model;
        QCOMPARE(model.rowCount(), 0);
    }

    void RoleNamesCount() {
        UploadTaskModel model;
        auto roles = model.roleNames();
        QCOMPARE(roles.size(), 13);
    }

    void AddTaskIncrementsRowCount() {
        UploadTaskModel model;

        UploadTask task;
        task.task_id = "upload_001";
        task.filename = "test.pdf";
        task.file_size = 1048576;
        task.status = "queued";
        model.AddTask(task);

        QCOMPARE(model.rowCount(), 1);
    }

    void DataReturnsCorrectValues() {
        UploadTaskModel model;

        UploadTask task;
        task.task_id = "upload_001";
        task.local_path = "/tmp/test.pdf";
        task.filename = "test.pdf";
        task.file_size = 1048576;
        task.file_hash = "abc123";
        task.parent_id = 42;
        task.status = "uploading";
        model.AddTask(task);

        auto idx = model.index(0);
        QCOMPARE(model.data(idx, UploadTaskModel::TaskIdRole).toString(), QString("upload_001"));
        QCOMPARE(model.data(idx, UploadTaskModel::LocalPathRole).toString(), QString("/tmp/test.pdf"));
        QCOMPARE(model.data(idx, UploadTaskModel::FilenameRole).toString(), QString("test.pdf"));
        QCOMPARE(model.data(idx, UploadTaskModel::FileSizeRole).toULongLong(), quint64(1048576));
        QCOMPARE(model.data(idx, UploadTaskModel::FileHashRole).toString(), QString("abc123"));
        QCOMPARE(model.data(idx, UploadTaskModel::ParentIdRole).toULongLong(), quint64(42));
        QCOMPARE(model.data(idx, UploadTaskModel::StatusRole).toString(), QString("uploading"));
    }

    void OptionalFieldsReturnInvalidWhenEmpty() {
        UploadTaskModel model;

        UploadTask task;
        task.task_id = "up_1";
        task.filename = "bare.txt";
        model.AddTask(task);

        auto idx = model.index(0);
        QCOMPARE(model.data(idx, UploadTaskModel::UploadIdRole), QVariant());
        QCOMPARE(model.data(idx, UploadTaskModel::ChunkSizeRole), QVariant());
        QCOMPARE(model.data(idx, UploadTaskModel::TotalChunksRole), QVariant());
        QCOMPARE(model.data(idx, UploadTaskModel::ErrorRole), QVariant());
    }

    void OptionalFieldsReturnValuesWhenSet() {
        UploadTaskModel model;

        UploadTask task;
        task.task_id = "up_2";
        task.filename = "doc.pdf";
        task.upload_id = "sess_abc";
        task.chunk_size = 5242880;
        task.total_chunks = 3;
        model.AddTask(task);

        auto idx = model.index(0);
        QCOMPARE(model.data(idx, UploadTaskModel::UploadIdRole).toString(), QString("sess_abc"));
        QCOMPARE(model.data(idx, UploadTaskModel::ChunkSizeRole).toInt(), 5242880);
        QCOMPARE(model.data(idx, UploadTaskModel::TotalChunksRole).toInt(), 3);
    }

    void UploadedChunkIndicesRole() {
        UploadTaskModel model;

        UploadTask task;
        task.task_id = "up_3";
        task.filename = "chunked.bin";
        task.uploaded_chunk_indices = { 0, 1, 2 };
        model.AddTask(task);

        auto idx = model.index(0);
        auto list = model.data(idx, UploadTaskModel::UploadedChunkIndicesRole).toList();
        QCOMPARE(list.size(), 3);
        QCOMPARE(list[0].toInt(), 0);
        QCOMPARE(list[2].toInt(), 2);
    }

    void InstantUploadRole() {
        UploadTaskModel model;

        UploadTask task;
        task.task_id = "up_4";
        task.filename = "instant.txt";
        task.instant_upload = true;
        model.AddTask(task);

        auto idx = model.index(0);
        QCOMPARE(model.data(idx, UploadTaskModel::InstantUploadRole).toBool(), true);
    }

    void ErrorRoleReturnsJsonObject() {
        UploadTaskModel model;

        UploadTask task;
        task.task_id = "up_err";
        task.filename = "failed.bin";
        task.status = "failed";
        ApiError err;
        err.code = 50004;
        err.message = "Storage quota exceeded";
        err.category = "StorageQuotaExceeded";
        task.error = err;
        model.AddTask(task);

        auto idx = model.index(0);
        auto err_var = model.data(idx, UploadTaskModel::ErrorRole);
        QVERIFY(err_var.isValid());
        auto err_obj = err_var.toJsonObject();
        QCOMPARE(err_obj["code"].toInt(), 50004);
        QCOMPARE(err_obj["message"].toString(), QString("Storage quota exceeded"));
    }

    void RemoveTask() {
        UploadTaskModel model;

        UploadTask a;
        a.task_id = "a";
        a.filename = "a.txt";
        UploadTask b;
        b.task_id = "b";
        b.filename = "b.txt";
        model.AddTask(a);
        model.AddTask(b);

        QVERIFY(model.RemoveTask("a"));
        QCOMPARE(model.rowCount(), 1);
        QCOMPARE(model.data(model.index(0), UploadTaskModel::TaskIdRole).toString(), QString("b"));
    }

    void RemoveNonexistentReturnsFalse() {
        UploadTaskModel model;
        QVERIFY(!model.RemoveTask("no_such_task"));
    }

    void UpdateTask() {
        UploadTaskModel model;

        UploadTask task;
        task.task_id = "up_mod";
        task.filename = "original.txt";
        task.status = "queued";
        model.AddTask(task);

        UploadTask updated;
        updated.task_id = "up_mod";
        updated.filename = "renamed.txt";
        updated.status = "completed";
        QVERIFY(model.UpdateTask("up_mod", updated));

        auto idx = model.index(0);
        QCOMPARE(model.data(idx, UploadTaskModel::FilenameRole).toString(), QString("renamed.txt"));
        QCOMPARE(model.data(idx, UploadTaskModel::StatusRole).toString(), QString("completed"));
    }

    void UpdateNonexistentReturnsFalse() {
        UploadTaskModel model;
        UploadTask task;
        task.task_id = "ghost";
        QVERIFY(!model.UpdateTask("ghost", task));
    }

    void ClearEmptiesModel() {
        UploadTaskModel model;
        UploadTask a;
        a.task_id = "a";
        UploadTask b;
        b.task_id = "b";
        model.AddTask(a);
        model.AddTask(b);
        QCOMPARE(model.rowCount(), 2);

        model.Clear();
        QCOMPARE(model.rowCount(), 0);
    }

    void FindTaskReturnsCorrectIndex() {
        UploadTaskModel model;
        UploadTask a, b, c;
        a.task_id = "t1";
        b.task_id = "t2";
        c.task_id = "t3";
        model.AddTask(a);
        model.AddTask(b);
        model.AddTask(c);

        QCOMPARE(model.FindTask("t1"), 0);
        QCOMPARE(model.FindTask("t2"), 1);
        QCOMPARE(model.FindTask("t3"), 2);
        QCOMPARE(model.FindTask("missing"), -1);
    }

    void IndexOfMatchesFindTask() {
        UploadTaskModel model;
        UploadTask a;
        a.task_id = "my_task";
        model.AddTask(a);

        QCOMPARE(model.indexOf("my_task"), 0);
        QCOMPARE(model.indexOf("nope"), -1);
    }

    void GetTaskReturnsCorrectItem() {
        UploadTaskModel model;
        UploadTask task;
        task.task_id = "fetch_me";
        task.filename = "target.dat";
        model.AddTask(task);

        auto retrieved = model.GetTask(0);
        QVERIFY(retrieved.has_value());
        QCOMPARE(retrieved->task_id, QString("fetch_me"));
        QCOMPARE(retrieved->filename, QString("target.dat"));

        auto invalid = model.GetTask(99);
        QVERIFY(!invalid.has_value());
    }

    void FromJsonParsesAllFields() {
        QJsonObject json;
        json["task_id"] = "json_task";
        json["local_path"] = "/home/user/file.txt";
        json["filename"] = "file.txt";
        json["file_size"] = 2048.0;
        json["file_hash"] = "md5hash";
        json["parent_id"] = 10.0;
        json["upload_id"] = "sess_xyz";
        json["chunk_size"] = 5242880;
        json["total_chunks"] = 1;
        json["uploaded_chunk_indices"] = QJsonArray{ 0 };
        json["instant_upload"] = false;
        json["status"] = "completed";

        auto task = UploadTask::FromJson(json);
        QCOMPARE(task.task_id, QString("json_task"));
        QCOMPARE(task.local_path, QString("/home/user/file.txt"));
        QCOMPARE(task.filename, QString("file.txt"));
        QCOMPARE(task.file_size, quint64(2048));
        QCOMPARE(task.file_hash, QString("md5hash"));
        QCOMPARE(task.parent_id, quint64(10));
        QVERIFY(task.upload_id.has_value());
        QCOMPARE(*task.upload_id, QString("sess_xyz"));
        QVERIFY(task.chunk_size.has_value());
        QCOMPARE(*task.chunk_size, 5242880);
        QVERIFY(task.total_chunks.has_value());
        QCOMPARE(*task.total_chunks, 1);
        QCOMPARE(task.uploaded_chunk_indices.size(), 1);
        QCOMPARE(task.status, QString("completed"));
        QVERIFY(!task.instant_upload);
    }

    void ToJsonRoundTrip() {
        UploadTask task;
        task.task_id = "rt_1";
        task.local_path = "/path/file.bin";
        task.filename = "file.bin";
        task.file_size = 4096;
        task.file_hash = "hash123";
        task.parent_id = 5;
        task.upload_id = "sess_rt";
        task.chunk_size = 4096;
        task.total_chunks = 1;
        task.uploaded_chunk_indices = { 0 };
        task.instant_upload = false;
        task.status = "uploading";

        auto json = task.ToJson();
        QCOMPARE(json["task_id"].toString(), QString("rt_1"));
        QCOMPARE(json["filename"].toString(), QString("file.bin"));
        QCOMPARE(json["file_size"].toDouble(), 4096.0);
        QCOMPARE(json["status"].toString(), QString("uploading"));
        QVERIFY(json.contains("upload_id"));
        QVERIFY(json.contains("uploaded_chunk_indices"));
    }
};

int run_TestUploadTaskModel(int argc, char* argv[]) {
    TestUploadTaskModel test;
    return QTest::qExec(&test, argc, argv);
}

#include "test_upload_task_model.moc"
