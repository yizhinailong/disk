#include <QTest>

#include "models/DriveListModel.hpp"

using namespace disk::desktop;

class TestDriveListModel : public QObject {
    Q_OBJECT

private slots:

    void InitEmpty() {
        DriveListModel model;
        QCOMPARE(model.rowCount(), 0);
    }

    void RoleNamesHas12Entries() {
        DriveListModel model;
        auto roles = model.roleNames();
        QCOMPARE(roles.size(), 12);
        QVERIFY(roles.contains(DriveListModel::IdRole));
        QVERIFY(roles.contains(DriveListModel::KindRole));
        QVERIFY(roles.contains(DriveListModel::NameRole));
        QVERIFY(roles.contains(DriveListModel::SizeRole));
        QVERIFY(roles.contains(DriveListModel::MimeTypeRole));
        QVERIFY(roles.contains(DriveListModel::HashRole));
        QVERIFY(roles.contains(DriveListModel::ItemCountRole));
        QVERIFY(roles.contains(DriveListModel::ParentIdRole));
        QVERIFY(roles.contains(DriveListModel::PathRole));
        QVERIFY(roles.contains(DriveListModel::CreatedAtRole));
        QVERIFY(roles.contains(DriveListModel::UpdatedAtRole));
        QVERIFY(roles.contains(DriveListModel::OriginRole));
    }

    void AddItemIncrementsRowCount() {
        DriveListModel model;

        DriveItem item;
        item.id = 1;
        item.kind = "file";
        item.name = "test.txt";
        model.AddItem(item);

        QCOMPARE(model.rowCount(), 1);
    }

    void DataReturnsCorrectValues() {
        DriveListModel model;

        DriveItem item;
        item.id = 42;
        item.kind = "file";
        item.name = "doc.pdf";
        item.size = 2048;
        item.mime_type = "application/pdf";
        item.origin = "file_list";
        model.AddItem(item);

        auto index = model.index(0);
        QCOMPARE(model.data(index, DriveListModel::IdRole).toUInt(), quint64(42));
        QCOMPARE(model.data(index, DriveListModel::KindRole).toString(), QString("file"));
        QCOMPARE(model.data(index, DriveListModel::NameRole).toString(), QString("doc.pdf"));
        QCOMPARE(model.data(index, DriveListModel::SizeRole).toULongLong(), quint64(2048));
        QCOMPARE(model.data(index, DriveListModel::MimeTypeRole).toString(), QString("application/pdf"));
        QCOMPARE(model.data(index, DriveListModel::OriginRole).toString(), QString("file_list"));
    }

    void OptionalFieldsReturnInvalidWhenEmpty() {
        DriveListModel model;

        DriveItem item;
        item.id = 1;
        item.kind = "folder";
        item.name = "empty_folder";
        model.AddItem(item);

        auto index = model.index(0);
        QCOMPARE(model.data(index, DriveListModel::SizeRole), QVariant());
        QCOMPARE(model.data(index, DriveListModel::MimeTypeRole), QVariant());
        QCOMPARE(model.data(index, DriveListModel::HashRole), QVariant());
    }

    void SetItemsReplacesAll() {
        DriveListModel model;

        DriveItem old_item;
        old_item.id = 1;
        old_item.name = "old";
        model.AddItem(old_item);
        QCOMPARE(model.rowCount(), 1);

        QVector<DriveItem> new_items;
        DriveItem a;
        a.id = 10;
        a.name = "new_a";
        DriveItem b;
        b.id = 20;
        b.name = "new_b";
        new_items.append(a);
        new_items.append(b);

        model.SetItems(new_items);
        QCOMPARE(model.rowCount(), 2);
        QCOMPARE(model.data(model.index(0), DriveListModel::IdRole).toUInt(), quint64(10));
        QCOMPARE(model.data(model.index(1), DriveListModel::IdRole).toUInt(), quint64(20));
    }

    void RemoveItemById() {
        DriveListModel model;

        DriveItem a;
        a.id = 100;
        a.name = "a";
        DriveItem b;
        b.id = 200;
        b.name = "b";
        model.AddItem(a);
        model.AddItem(b);

        QVERIFY(model.RemoveItem(100));
        QCOMPARE(model.rowCount(), 1);
        QCOMPARE(model.data(model.index(0), DriveListModel::IdRole).toUInt(), quint64(200));
    }

    void RemoveNonexistentReturnsFalse() {
        DriveListModel model;
        QVERIFY(!model.RemoveItem(999));
    }

    void ClearEmptiesModel() {
        DriveListModel model;
        DriveItem item;
        item.id = 1;
        model.AddItem(item);
        model.AddItem(item);
        QCOMPARE(model.rowCount(), 2);

        model.Clear();
        QCOMPARE(model.rowCount(), 0);
    }

    void IndexOfReturnsCorrectPosition() {
        DriveListModel model;

        DriveItem a;
        a.id = 10;
        DriveItem b;
        b.id = 20;
        DriveItem c;
        c.id = 30;
        model.AddItem(a);
        model.AddItem(b);
        model.AddItem(c);

        QCOMPARE(model.indexOf(10), 0);
        QCOMPARE(model.indexOf(20), 1);
        QCOMPARE(model.indexOf(30), 2);
        QCOMPARE(model.indexOf(99), -1);
    }

    void GetItemReturnsCorrectItem() {
        DriveListModel model;

        DriveItem item;
        item.id = 42;
        item.name = "target";
        model.AddItem(item);

        auto retrieved = model.GetItem(0);
        QVERIFY(retrieved.has_value());
        QCOMPARE(retrieved->id, quint64(42));
        QCOMPARE(retrieved->name, QString("target"));

        auto invalid = model.GetItem(99);
        QVERIFY(!invalid.has_value());
    }

    void InvalidIndexReturnsEmptyData() {
        DriveListModel model;
        auto index = model.index(0);
        QCOMPARE(model.data(index, DriveListModel::IdRole), QVariant());

        index = model.index(-1);
        QCOMPARE(model.data(index, DriveListModel::IdRole), QVariant());
    }

    void AddItemsBatch() {
        DriveListModel model;

        QVector<DriveItem> items;
        for (int i = 0; i < 5; ++i) {
            DriveItem item;
            item.id = static_cast<quint64>(i + 1);
            item.name = QString("item_%1").arg(i);
            items.append(item);
        }

        model.AddItems(items);
        QCOMPARE(model.rowCount(), 5);
        QCOMPARE(model.data(model.index(4), DriveListModel::IdRole).toUInt(), quint64(5));
    }

    void AddEmptyItemsIsNoop() {
        DriveListModel model;
        QVector<DriveItem> empty;
        model.AddItems(empty);
        QCOMPARE(model.rowCount(), 0);
    }
};

int run_TestDriveListModel(int argc, char* argv[]) {
    TestDriveListModel test;
    return QTest::qExec(&test, argc, argv);
}

#include "test_drive_list_model.moc"
