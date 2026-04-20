#include <QDateTime>
#include <QJsonArray>
#include <QJsonObject>
#include <QTest>

#include "helpers/TestJsonLoader.hpp"
#include "models/DriveItem.hpp"

using namespace disk::desktop;
using namespace disk::desktop::testing;

class TestDriveItemMapping : public QObject {
    Q_OBJECT

private slots:

    void FileTypeMapping() {
        QJsonObject json;
        json["id"] = 1.0;
        json["type"] = "file";
        json["name"] = "report.pdf";
        json["size"] = 1048576.0;
        json["mime_type"] = "application/pdf";
        json["hash"] = "d41d8cd98f00b204e9800998ecf8427e";
        json["parent_id"] = 0.0;
        json["created_at"] = "2026-01-15T10:30:00";
        json["updated_at"] = "2026-01-15T11:00:00";

        auto item = DriveItem::FromJson(json, "file_list");

        QCOMPARE(item.id, quint64(1));
        QCOMPARE(item.kind, QString("file"));
        QCOMPARE(item.name, QString("report.pdf"));
        QVERIFY(item.size.has_value());
        QCOMPARE(*item.size, quint64(1048576));
        QVERIFY(item.mime_type.has_value());
        QCOMPARE(*item.mime_type, QString("application/pdf"));
        QVERIFY(item.hash.has_value());
        QCOMPARE(*item.hash, QString("d41d8cd98f00b204e9800998ecf8427e"));
        QVERIFY(!item.item_count.has_value());
        QCOMPARE(item.origin, QString("file_list"));
        QVERIFY(item.created_at.has_value());
        QVERIFY(item.updated_at.has_value());
    }

    void FolderTypeMapping() {
        QJsonObject json;
        json["id"] = 2.0;
        json["type"] = "folder";
        json["name"] = "Projects";
        json["item_count"] = 5;
        json["parent_id"] = 0.0;
        json["created_at"] = "2026-01-10T08:00:00";
        json["updated_at"] = "2026-01-10T08:00:00";

        auto item = DriveItem::FromJson(json, "file_list");

        QCOMPARE(item.kind, QString("folder"));
        QVERIFY(!item.mime_type.has_value());
        QVERIFY(!item.hash.has_value());
        QVERIFY(item.item_count.has_value());
        QCOMPARE(*item.item_count, 5);
    }

    void ShareBrowseFolderSizeIsNull() {
        QJsonObject json;
        json["id"] = 202.0;
        json["type"] = "folder";
        json["name"] = "archive";
        json["size"] = 0.0;

        auto item = DriveItem::FromJson(json, "share_browse");

        QCOMPARE(item.kind, QString("folder"));
        QVERIFY(!item.size.has_value());
    }

    void ShareBrowseFileSizePreserved() {
        QJsonObject json;
        json["id"] = 201.0;
        json["type"] = "file";
        json["name"] = "presentation.pptx";
        json["size"] = 3145728.0;
        json["mime_type"] = "application/vnd.openxmlformats-officedocument.presentationml.presentation";

        auto item = DriveItem::FromJson(json, "share_browse");

        QCOMPARE(item.kind, QString("file"));
        QVERIFY(item.size.has_value());
        QCOMPARE(*item.size, quint64(3145728));
    }

    void FileListFolderSizeNotNulled() {
        QJsonObject json;
        json["id"] = 10.0;
        json["type"] = "folder";
        json["name"] = "docs";
        json["size"] = 0.0;

        auto item = DriveItem::FromJson(json, "file_list");

        QCOMPARE(item.kind, QString("folder"));
        QVERIFY(item.size.has_value());
        QCOMPARE(*item.size, quint64(0));
    }

    void SearchResponseIncludesPath() {
        QJsonObject json;
        json["id"] = 10.0;
        json["type"] = "file";
        json["name"] = "notes.md";
        json["size"] = 512.0;
        json["path"] = "/Documents/notes.md";

        auto item = DriveItem::FromJson(json, "search");

        QVERIFY(item.path.has_value());
        QCOMPARE(*item.path, QString("/Documents/notes.md"));
        QCOMPARE(item.origin, QString("search"));
    }

    void MissingOptionalFieldsAreNullopt() {
        QJsonObject json;
        json["id"] = 99.0;
        json["type"] = "file";
        json["name"] = "bare.txt";

        auto item = DriveItem::FromJson(json, "file_list");

        QVERIFY(!item.parent_id.has_value());
        QVERIFY(!item.path.has_value());
        QVERIFY(!item.created_at.has_value());
        QVERIFY(!item.updated_at.has_value());
        QVERIFY(!item.mime_type.has_value());
        QVERIFY(!item.hash.has_value());
        QVERIFY(!item.size.has_value());
    }

    void ToJsonRoundTrip() {
        QJsonObject json;
        json["id"] = 1.0;
        json["type"] = "file";
        json["name"] = "report.pdf";
        json["size"] = 1048576.0;
        json["mime_type"] = "application/pdf";
        json["hash"] = "d41d8cd98f00b204e9800998ecf8427e";

        auto item = DriveItem::FromJson(json, "file_list");
        auto out = item.ToJson();

        QCOMPARE(out["id"].toDouble(), 1.0);
        QCOMPARE(out["type"].toString(), QString("file"));
        QCOMPARE(out["name"].toString(), QString("report.pdf"));
        QCOMPARE(out["size"].toDouble(), 1048576.0);
        QCOMPARE(out["mime_type"].toString(), QString("application/pdf"));
        QCOMPARE(out["hash"].toString(), QString("d41d8cd98f00b204e9800998ecf8427e"));
        QCOMPARE(out["origin"].toString(), QString("file_list"));
    }

    void FixtureFileLoading() {
        auto fixture = TestJsonLoader::LoadJson("models/file_list_response.json");
        QVERIFY(!fixture.isEmpty());

        auto data = fixture["data"].toObject();
        auto items = data["items"].toArray();
        QVERIFY(items.size() >= 3);

        auto file_item = DriveItem::FromJson(items[0].toObject(), "file_list");
        QCOMPARE(file_item.kind, QString("file"));
        QCOMPARE(file_item.name, QString("report.pdf"));

        auto folder_item = DriveItem::FromJson(items[1].toObject(), "file_list");
        QCOMPARE(folder_item.kind, QString("folder"));
        QCOMPARE(folder_item.name, QString("Projects"));
    }
};

#include "test_drive_item.moc"
