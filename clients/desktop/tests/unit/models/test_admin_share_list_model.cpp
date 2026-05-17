#include <QTest>

#include <QJsonArray>
#include <QJsonValue>

#include "models/AdminShareListModel.hpp"
#include "helpers/TestJsonLoader.hpp"

using namespace disk::desktop;

class TestAdminShareListModel : public QObject {
    Q_OBJECT

private slots:

    void InitEmpty() {
        AdminShareListModel model;
        QCOMPARE(model.rowCount(), 0);
    }

    void RoleNamesHas10Entries() {
        AdminShareListModel model;
        auto roles = model.roleNames();
        QCOMPARE(roles.size(), 10);
        QVERIFY(roles.contains(AdminShareListModel::IdRole));
        QVERIFY(roles.contains(AdminShareListModel::UserIdRole));
        QVERIFY(roles.contains(AdminShareListModel::UsernameRole));
        QVERIFY(roles.contains(AdminShareListModel::FileIdRole));
        QVERIFY(roles.contains(AdminShareListModel::FileNameRole));
        QVERIFY(roles.contains(AdminShareListModel::ShareCodeRole));
        QVERIFY(roles.contains(AdminShareListModel::StatusRole));
        QVERIFY(roles.contains(AdminShareListModel::AccessCountRole));
        QVERIFY(roles.contains(AdminShareListModel::CreatedAtRole));
        QVERIFY(roles.contains(AdminShareListModel::ExpiresAtRole));
    }

    void SetItemsReplacesAll() {
        AdminShareListModel model;

        AdminShareItem old_item;
        old_item.id = 1;
        old_item.username = "old_user";
        model.SetItems({ old_item });
        QCOMPARE(model.rowCount(), 1);

        AdminShareItem a;
        a.id = 10;
        a.username = "user_a";
        AdminShareItem b;
        b.id = 20;
        b.username = "user_b";
        model.SetItems({ a, b });
        QCOMPARE(model.rowCount(), 2);
        QCOMPARE(model.data(model.index(0), AdminShareListModel::IdRole).toUInt(), quint64(10));
        QCOMPARE(model.data(model.index(1), AdminShareListModel::IdRole).toUInt(), quint64(20));
    }

    void DataReturnsCorrectValues() {
        AdminShareListModel model;

        AdminShareItem item;
        item.id = 42;
        item.user_id = 100;
        item.username = "alice";
        item.file_id = 5001;
        item.file_name = "project_plan.pdf";
        item.share_code = "sh_alice_abc123";
        item.status = 1;
        item.access_count = 15;
        item.created_at = "2026-03-20T10:00:00";
        item.expires_at = "2026-04-20T10:00:00";
        model.SetItems({ item });

        auto index = model.index(0);
        QCOMPARE(model.data(index, AdminShareListModel::IdRole).toUInt(), quint64(42));
        QCOMPARE(model.data(index, AdminShareListModel::UserIdRole).toUInt(), quint64(100));
        QCOMPARE(model.data(index, AdminShareListModel::UsernameRole).toString(), QString("alice"));
        QCOMPARE(model.data(index, AdminShareListModel::FileIdRole).toUInt(), quint64(5001));
        QCOMPARE(model.data(index, AdminShareListModel::FileNameRole).toString(), QString("project_plan.pdf"));
        QCOMPARE(model.data(index, AdminShareListModel::ShareCodeRole).toString(), QString("sh_alice_abc123"));
        QCOMPARE(model.data(index, AdminShareListModel::StatusRole).toInt(), 1);
        QCOMPARE(model.data(index, AdminShareListModel::AccessCountRole).toInt(), 15);
        QCOMPARE(model.data(index, AdminShareListModel::CreatedAtRole).toString(), QString("2026-03-20T10:00:00"));
        QCOMPARE(model.data(index, AdminShareListModel::ExpiresAtRole).toString(), QString("2026-04-20T10:00:00"));
    }

    void GetItemReturnsCorrectItem() {
        AdminShareListModel model;

        AdminShareItem item;
        item.id = 42;
        item.username = "alice";
        model.SetItems({ item });

        auto retrieved = model.GetItem(0);
        QVERIFY(retrieved.has_value());
        QCOMPARE(retrieved->id, quint64(42));
        QCOMPARE(retrieved->username, QString("alice"));

        auto invalid = model.GetItem(99);
        QVERIFY(!invalid.has_value());
    }

    void GetItemOutOfBounds() {
        AdminShareListModel model;
        QVERIFY(!model.GetItem(0).has_value());
        QVERIFY(!model.GetItem(-1).has_value());
    }

    void ClearEmptiesModel() {
        AdminShareListModel model;
        AdminShareItem item;
        item.id = 1;
        model.SetItems({ item, item });
        QCOMPARE(model.rowCount(), 2);

        model.Clear();
        QCOMPARE(model.rowCount(), 0);
    }

    void ClearEmptyModelIsNoop() {
        AdminShareListModel model;
        model.Clear();
        QCOMPARE(model.rowCount(), 0);
    }

    void FromJsonParsesAllFields() {
        QJsonObject json{
            { "id", 1 },
            { "user_id", 100 },
            { "username", "alice" },
            { "file_id", 5001 },
            { "file_name", "project_plan.pdf" },
            { "share_code", "sh_alice_abc123" },
            { "status", 1 },
            { "access_count", 15 },
            { "created_at", "2026-03-20T10:00:00" },
            { "expires_at", "2026-04-20T10:00:00" },
        };

        auto item = AdminShareItem::FromJson(json);
        QCOMPARE(item.id, quint64(1));
        QCOMPARE(item.user_id, quint64(100));
        QCOMPARE(item.username, QString("alice"));
        QCOMPARE(item.file_id, quint64(5001));
        QCOMPARE(item.file_name, QString("project_plan.pdf"));
        QCOMPARE(item.share_code, QString("sh_alice_abc123"));
        QCOMPARE(item.status, 1);
        QCOMPARE(item.access_count, 15);
        QCOMPARE(item.created_at, QString("2026-03-20T10:00:00"));
        QCOMPARE(item.expires_at, QString("2026-04-20T10:00:00"));
    }

    void FromJsonHandlesMissingFields() {
        QJsonObject json{
            { "id", 5 },
            { "username", "bob" },
        };

        auto item = AdminShareItem::FromJson(json);
        QCOMPARE(item.id, quint64(5));
        QCOMPARE(item.username, QString("bob"));
        QCOMPARE(item.user_id, quint64(0));
        QCOMPARE(item.status, 0);
        QCOMPARE(item.access_count, 0);
    }

    void LoadFromFixtureFile() {
        auto json = testing::TestJsonLoader::LoadJson("admin/admin_list_shares_success.json");
        QVERIFY(json.contains("data"));
        auto data = json.value("data").toObject();
        QVERIFY(data.contains("items"));
        auto items = data.value("items").toArray();
        QCOMPARE(items.size(), 2);

        QVector<AdminShareItem> share_items;
        for (auto&& v : items) {
            share_items.append(AdminShareItem::FromJson(v.toObject()));
        }
        AdminShareListModel model;
        model.SetItems(share_items);
        QCOMPARE(model.rowCount(), 2);
        QCOMPARE(model.data(model.index(0), AdminShareListModel::IdRole).toUInt(), quint64(1));
        QCOMPARE(model.data(model.index(0), AdminShareListModel::UsernameRole).toString(), QString("alice"));
        QCOMPARE(model.data(model.index(1), AdminShareListModel::IdRole).toUInt(), quint64(2));
        QCOMPARE(model.data(model.index(1), AdminShareListModel::StatusRole).toInt(), 2);
    }
};

int run_TestAdminShareListModel(int argc, char* argv[]) {
    TestAdminShareListModel test;
    return QTest::qExec(&test, argc, argv);
}

#include "test_admin_share_list_model.moc"