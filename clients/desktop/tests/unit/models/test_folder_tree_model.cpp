#include <QJsonArray>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTest>

#include "models/FolderTreeModel.hpp"

using namespace disk::desktop;

class TestFolderTreeModel : public QObject {
    Q_OBJECT

private slots:

    void InitEmpty() {
        FolderTreeModel model;
        QCOMPARE(model.rowCount(), 0);
    }

    void RoleNamesHasTwoEntries() {
        FolderTreeModel model;
        auto roles = model.roleNames();
        QCOMPARE(roles.size(), 2);
        QVERIFY(roles.contains(FolderTreeModel::IdRole));
        QVERIFY(roles.contains(FolderTreeModel::NameRole));
    }

    void ColumnCountIsOne() {
        FolderTreeModel model;
        QCOMPARE(model.columnCount(), 1);
    }

    void SetRootCreatesTopLevelNodes() {
        FolderTreeModel model;

        FolderNode root;
        root.id = 0;
        root.name = "My Drive";

        FolderNode child1;
        child1.id = 10;
        child1.name = "Documents";

        FolderNode child2;
        child2.id = 20;
        child2.name = "Photos";

        root.children.append(child1);
        root.children.append(child2);

        model.SetRoot(root);

        QCOMPARE(model.rowCount(), 2);

        auto idx0 = model.index(0, 0);
        QCOMPARE(model.data(idx0, FolderTreeModel::IdRole).toULongLong(), quint64(10));
        QCOMPARE(model.data(idx0, FolderTreeModel::NameRole).toString(), QString("Documents"));

        auto idx1 = model.index(1, 0);
        QCOMPARE(model.data(idx1, FolderTreeModel::IdRole).toULongLong(), quint64(20));
        QCOMPARE(model.data(idx1, FolderTreeModel::NameRole).toString(), QString("Photos"));
    }

    void NestedChildrenAreAccessible() {
        FolderTreeModel model;

        FolderNode root;
        root.id = 0;
        root.name = "Root";

        FolderNode docs;
        docs.id = 1;
        docs.name = "Docs";

        FolderNode sub;
        sub.id = 2;
        sub.name = "Projects";
        docs.children.append(sub);

        root.children.append(docs);
        model.SetRoot(root);

        QCOMPARE(model.rowCount(), 1);

        auto docs_idx = model.index(0, 0);
        QCOMPARE(model.rowCount(docs_idx), 1);

        auto sub_idx = model.index(0, 0, docs_idx);
        QVERIFY(sub_idx.isValid());
        QCOMPARE(model.data(sub_idx, FolderTreeModel::IdRole).toULongLong(), quint64(2));
        QCOMPARE(model.data(sub_idx, FolderTreeModel::NameRole).toString(), QString("Projects"));
    }

    void ParentIndexWorks() {
        FolderTreeModel model;

        FolderNode root;
        root.id = 0;
        root.name = "Root";

        FolderNode child;
        child.id = 1;
        child.name = "Child";
        root.children.append(child);

        model.SetRoot(root);

        auto child_idx = model.index(0, 0);
        auto parent = model.parent(child_idx);
        QVERIFY(!parent.isValid());

        FolderNode grandchild;
        grandchild.id = 2;
        grandchild.name = "Grandchild";
        root.children[0].children.append(grandchild);

        model.SetRoot(root);

        auto child_idx2 = model.index(0, 0);
        QCOMPARE(model.rowCount(child_idx2), 1);

        auto gc_idx = model.index(0, 0, child_idx2);
        QVERIFY(gc_idx.isValid());

        auto gc_parent = model.parent(gc_idx);
        QVERIFY(gc_parent.isValid());
        QCOMPARE(model.data(gc_parent, FolderTreeModel::IdRole).toULongLong(), quint64(1));
    }

    void InvalidIndexReturnsEmptyData() {
        FolderTreeModel model;

        auto idx = model.index(-1, 0);
        QCOMPARE(model.data(idx, FolderTreeModel::IdRole), QVariant());

        idx = model.index(0, 0);
        QCOMPARE(model.data(idx, FolderTreeModel::IdRole), QVariant());
    }

    void GetNodeReturnsRootForInvalidIndex() {
        FolderTreeModel model;

        FolderNode root;
        root.id = 99;
        root.name = "RootNode";
        model.SetRoot(root);

        auto node = model.GetNode(QModelIndex());
        QVERIFY(node != nullptr);
        QCOMPARE(node->id, quint64(99));
        QCOMPARE(node->name, QString("RootNode"));
    }

    void IndexOfFindsNode() {
        FolderTreeModel model;

        FolderNode root;
        root.id = 0;
        root.name = "Root";

        FolderNode a;
        a.id = 10;
        a.name = "A";

        FolderNode b;
        b.id = 20;
        b.name = "B";

        root.children.append(a);
        root.children.append(b);
        model.SetRoot(root);

        auto idx_a = model.indexOf(10);
        QVERIFY(idx_a.isValid());
        QCOMPARE(model.data(idx_a, FolderTreeModel::IdRole).toULongLong(), quint64(10));

        auto idx_b = model.indexOf(20);
        QVERIFY(idx_b.isValid());

        auto idx_missing = model.indexOf(999);
        QVERIFY(!idx_missing.isValid());
    }

    void FolderNodeFromJson() {
        QJsonObject json;
        json["id"] = 42.0;
        json["name"] = "TestFolder";

        auto node = FolderNode::FromJson(json);
        QCOMPARE(node.id, quint64(42));
        QCOMPARE(node.name, QString("TestFolder"));
        QVERIFY(node.children.isEmpty());
    }

    void FolderNodeFromJsonWithChildren() {
        QJsonObject json;
        json["id"] = 1.0;
        json["name"] = "Parent";
        json["children"] = QJsonArray{
            QJsonObject{ { "id", 2.0 }, { "name", "Child1" } },
            QJsonObject{ { "id", 3.0 }, { "name", "Child2" } },
        };

        auto node = FolderNode::FromJson(json);
        QCOMPARE(node.children.size(), 2);
        QCOMPARE(node.children[0].id, quint64(2));
        QCOMPARE(node.children[1].name, QString("Child2"));
    }

    void FolderNodeToJsonRoundTrip() {
        FolderNode node;
        node.id = 5;
        node.name = "Folder";

        FolderNode child;
        child.id = 6;
        child.name = "Sub";
        node.children.append(child);

        auto json = node.ToJson();
        QCOMPARE(json.value("id").toDouble(), 5.0);
        QCOMPARE(json.value("name").toString(), QString("Folder"));
        QVERIFY(json.contains("children"));
        QCOMPARE(json.value("children").toArray().size(), 1);
    }

    void SetRootResetsModel() {
        FolderTreeModel model;

        FolderNode first;
        first.id = 0;
        first.name = "First";
        FolderNode c;
        c.id = 1;
        c.name = "C1";
        first.children.append(c);
        model.SetRoot(first);
        QCOMPARE(model.rowCount(), 1);

        FolderNode second;
        second.id = 0;
        second.name = "Second";
        model.SetRoot(second);
        QCOMPARE(model.rowCount(), 0);
    }
};

int run_TestFolderTreeModel(int argc, char* argv[]) {
    TestFolderTreeModel test;
    return QTest::qExec(&test, argc, argv);
}

#include "test_folder_tree_model.moc"
