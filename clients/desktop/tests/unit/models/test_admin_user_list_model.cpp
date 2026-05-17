#include <QTest>
#include <QJsonObject>

#include "models/AdminUserListModel.hpp"

using namespace disk::desktop;

class TestAdminUserListModel : public QObject {
    Q_OBJECT

private slots:

    void InitEmpty() {
        AdminUserListModel model;
        QCOMPARE(model.rowCount(), 0);
    }

    void RoleNamesHas10Entries() {
        AdminUserListModel model;
        auto roles = model.roleNames();
        QCOMPARE(roles.size(), 10);
        QVERIFY(roles.contains(AdminUserListModel::IdRole));
        QVERIFY(roles.contains(AdminUserListModel::UsernameRole));
        QVERIFY(roles.contains(AdminUserListModel::EmailRole));
        QVERIFY(roles.contains(AdminUserListModel::NicknameRole));
        QVERIFY(roles.contains(AdminUserListModel::RoleRole));
        QVERIFY(roles.contains(AdminUserListModel::StatusRole));
        QVERIFY(roles.contains(AdminUserListModel::StorageQuotaRole));
        QVERIFY(roles.contains(AdminUserListModel::StorageUsedRole));
        QVERIFY(roles.contains(AdminUserListModel::CreatedAtRole));
        QVERIFY(roles.contains(AdminUserListModel::LastLoginAtRole));
    }

    void SetItemsPopulatesModel() {
        AdminUserListModel model;

        QVector<AdminUserItem> items;
        AdminUserItem a;
        a.id = 1;
        a.username = "alice";
        a.email = "alice@example.com";
        items.append(a);
        AdminUserItem b;
        b.id = 2;
        b.username = "bob";
        b.email = "bob@example.com";
        items.append(b);

        model.SetItems(items);
        QCOMPARE(model.rowCount(), 2);
        auto first = model.GetItem(0);
        QVERIFY(first.has_value());
        QCOMPARE(first->id, quint64(1));
        auto second = model.GetItem(1);
        QVERIFY(second.has_value());
        QCOMPARE(second->id, quint64(2));
    }

    void RowCountCorrect() {
        AdminUserListModel model;
        QVector<AdminUserItem> items;
        for (int i = 0; i < 5; ++i) {
            AdminUserItem item;
            item.id = static_cast<quint64>(i + 1);
            items.append(item);
        }
        model.SetItems(items);
        QCOMPARE(model.rowCount(), 5);
    }

    void DataReturnsCorrectRoleValues() {
        AdminUserListModel model;

        AdminUserItem item;
        item.id = 42;
        item.username = "admin_user";
        item.email = "admin@example.com";
        item.nickname = "Admin User";
        item.role = 1;
        item.status = 1;
        item.storage_quota = 10737418240;
        item.storage_used = 5368709120;
        item.created_at = "2026-01-15T08:00:00";
        item.last_login_at = "2026-05-10T14:30:00";

        QVector<AdminUserItem> items;
        items.append(item);
        model.SetItems(items);

        auto index = model.index(0);
        QCOMPARE(model.data(index, AdminUserListModel::IdRole).toUInt(), quint64(42));
        QCOMPARE(model.data(index, AdminUserListModel::UsernameRole).toString(), QString("admin_user"));
        QCOMPARE(model.data(index, AdminUserListModel::EmailRole).toString(), QString("admin@example.com"));
        QCOMPARE(model.data(index, AdminUserListModel::NicknameRole).toString(), QString("Admin User"));
        QCOMPARE(model.data(index, AdminUserListModel::RoleRole).toInt(), 1);
        QCOMPARE(model.data(index, AdminUserListModel::StatusRole).toInt(), 1);
        QCOMPARE(model.data(index, AdminUserListModel::StorageQuotaRole).toLongLong(), qint64(10737418240));
        QCOMPARE(model.data(index, AdminUserListModel::StorageUsedRole).toLongLong(), qint64(5368709120));
        QCOMPARE(model.data(index, AdminUserListModel::CreatedAtRole).toString(), QString("2026-01-15T08:00:00"));
        QCOMPARE(model.data(index, AdminUserListModel::LastLoginAtRole).toString(), QString("2026-05-10T14:30:00"));
    }

    void EmptyModelReturnsInvalid() {
        AdminUserListModel model;
        QVERIFY(!model.GetItem(0).has_value());
    }

    void GetItemReturnsCorrectData() {
        AdminUserListModel model;

        AdminUserItem item;
        item.id = 99;
        item.username = "testuser";
        item.email = "test@example.com";
        item.nickname = "Test User";
        item.role = 0;
        item.status = 2;
        item.storage_quota = 2147483648;
        item.storage_used = 1073741824;

        QVector<AdminUserItem> items;
        items.append(item);
        model.SetItems(items);

        auto retrieved = model.GetItem(0);
        QVERIFY(retrieved.has_value());
        QCOMPARE(retrieved->id, quint64(99));
        QCOMPARE(retrieved->username, QString("testuser"));
        QCOMPARE(retrieved->email, QString("test@example.com"));
        QCOMPARE(retrieved->nickname, QString("Test User"));
        QCOMPARE(retrieved->role, 0);
        QCOMPARE(retrieved->status, 2);

        auto invalid = model.GetItem(999);
        QVERIFY(!invalid.has_value());
    }

    void ClearEmptiesModel() {
        AdminUserListModel model;

        AdminUserItem item;
        item.id = 1;
        item.username = "user1";
        QVector<AdminUserItem> items;
        items.append(item);
        items.append(item);
        model.SetItems(items);
        QCOMPARE(model.rowCount(), 2);

        model.Clear();
        QCOMPARE(model.rowCount(), 0);
        QVERIFY(!model.GetItem(0).has_value());
    }

    void FromJsonParsesAllFields() {
        QJsonObject json;
        json["id"] = 123;
        json["username"] = "json_user";
        json["email"] = "json@example.com";
        json["nickname"] = "JSON User";
        json["role"] = 1;
        json["status"] = 0;
        json["storage_quota"] = 4294967296.0;
        json["storage_used"] = 2147483648.0;
        json["created_at"] = "2026-02-01T10:00:00";
        json["last_login_at"] = "2026-05-01T12:00:00";

        auto item = AdminUserItem::FromJson(json);
        QCOMPARE(item.id, quint64(123));
        QCOMPARE(item.username, QString("json_user"));
        QCOMPARE(item.email, QString("json@example.com"));
        QCOMPARE(item.nickname, QString("JSON User"));
        QCOMPARE(item.role, 1);
        QCOMPARE(item.status, 0);
        QCOMPARE(item.storage_quota, qint64(4294967296));
        QCOMPARE(item.storage_used, qint64(2147483648));
        QCOMPARE(item.created_at, QString("2026-02-01T10:00:00"));
        QCOMPARE(item.last_login_at, QString("2026-05-01T12:00:00"));
    }

    void FromJsonWithDefaults() {
        QJsonObject json;
        json["id"] = 1;
        json["username"] = "minimal";

        auto item = AdminUserItem::FromJson(json);
        QCOMPARE(item.id, quint64(1));
        QCOMPARE(item.username, QString("minimal"));
        QVERIFY(item.email.isEmpty());
        QVERIFY(item.nickname.isEmpty());
        QCOMPARE(item.role, 0);
        QCOMPARE(item.status, 1);
        QCOMPARE(item.storage_quota, qint64(0));
        QCOMPARE(item.storage_used, qint64(0));
        QVERIFY(item.created_at.isEmpty());
        QVERIFY(item.last_login_at.isEmpty());
    }
};

int run_TestAdminUserListModel(int argc, char* argv[]) {
    TestAdminUserListModel test;
    return QTest::qExec(&test, argc, argv);
}

#include "test_admin_user_list_model.moc"