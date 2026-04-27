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

    void RoleNamesHasThreeEntries() {
        FolderTreeModel model;
        auto roles = model.roleNames();
        QCOMPARE(roles.size(), 3);
        QVERIFY(roles.contains(FolderTreeModel::IdRole));
        QVERIFY(roles.contains(FolderTreeModel::NameRole));
        QVERIFY(roles.contains(FolderTreeModel::DepthRole));
    }

    void RoleNamesMatchQmlExpectations() {
        FolderTreeModel model;
        auto roles = model.roleNames();

        // QML FolderTreePanel delegate reads: model.id, model.name, model.depth
        QCOMPARE(roles.value(FolderTreeModel::IdRole), QByteArray("id"));
        QCOMPARE(roles.value(FolderTreeModel::NameRole), QByteArray("name"));
        QCOMPARE(roles.value(FolderTreeModel::DepthRole), QByteArray("depth"));
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

    void DepthRoleIsZeroForTopLevelNodes() {
        FolderTreeModel model;

        FolderNode root;
        root.id = 0;
        root.name = "My Drive";

        FolderNode a;
        a.id = 10;
        a.name = "A";
        FolderNode b;
        b.id = 20;
        b.name = "B";

        root.children.append(a);
        root.children.append(b);
        model.SetRoot(root);

        QCOMPARE(model.data(model.index(0, 0), FolderTreeModel::DepthRole).toInt(), 0);
        QCOMPARE(model.data(model.index(1, 0), FolderTreeModel::DepthRole).toInt(), 0);
    }

    void DepthRoleIsOneForSecondLevelNodes() {
        FolderTreeModel model;

        FolderNode root;
        root.id = 0;
        root.name = "My Drive";

        FolderNode docs;
        docs.id = 1;
        docs.name = "Docs";

        FolderNode sub;
        sub.id = 2;
        sub.name = "Projects";
        docs.children.append(sub);

        root.children.append(docs);
        model.SetRoot(root);

        auto docs_idx = model.index(0, 0);
        QCOMPARE(model.data(docs_idx, FolderTreeModel::DepthRole).toInt(), 0);

        auto sub_idx = model.index(0, 0, docs_idx);
        QCOMPARE(model.data(sub_idx, FolderTreeModel::DepthRole).toInt(), 1);
    }

    void DepthRoleIsTwoForThirdLevelNodes() {
        FolderTreeModel model;

        FolderNode root;
        root.id = 0;
        root.name = "My Drive";

        FolderNode docs;
        docs.id = 1;
        docs.name = "Docs";

        FolderNode work;
        work.id = 2;
        work.name = "Work";

        FolderNode project;
        project.id = 3;
        project.name = "ProjectAlpha";
        work.children.append(project);

        docs.children.append(work);
        root.children.append(docs);
        model.SetRoot(root);

        auto docs_idx = model.index(0, 0);
        auto work_idx = model.index(0, 0, docs_idx);
        auto project_idx = model.index(0, 0, work_idx);

        QCOMPARE(model.data(docs_idx, FolderTreeModel::DepthRole).toInt(), 0);
        QCOMPARE(model.data(work_idx, FolderTreeModel::DepthRole).toInt(), 1);
        QCOMPARE(model.data(project_idx, FolderTreeModel::DepthRole).toInt(), 2);
    }

    void DepthRoleReturnsZeroForInvalidIndex() {
        FolderTreeModel model;

        auto invalid = QModelIndex();
        QCOMPARE(model.data(invalid, FolderTreeModel::DepthRole), QVariant());
    }

    void HasChildrenReturnsFalseForEmptyModel() {
        FolderTreeModel model;
        QVERIFY(!model.hasChildren());
    }

    void HasChildrenReturnsTrueWhenTopLevelNodesExist() {
        FolderTreeModel model;

        FolderNode root;
        root.id = 0;
        root.name = "Root";

        FolderNode a;
        a.id = 10;
        a.name = "A";
        root.children.append(a);

        model.SetRoot(root);

        QVERIFY(model.hasChildren());
        QVERIFY(model.hasChildren({}));
    }

    void HasChildrenReturnsTrueForParentWithChildren() {
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

        auto docs_idx = model.index(0, 0);
        QVERIFY(model.hasChildren(docs_idx));
    }

    void HasChildrenReturnsFalseForLeafNode() {
        FolderTreeModel model;

        FolderNode root;
        root.id = 0;
        root.name = "Root";

        FolderNode leaf;
        leaf.id = 10;
        leaf.name = "Leaf";
        root.children.append(leaf);

        model.SetRoot(root);

        auto leaf_idx = model.index(0, 0);
        QVERIFY(!model.hasChildren(leaf_idx));
    }

    void AncestorPathReturnsEmptyForMissingId() {
        FolderTreeModel model;

        FolderNode root;
        root.id = 0;
        root.name = "Root";
        model.SetRoot(root);

        auto path = model.ancestorPath(999);
        QVERIFY(path.isEmpty());
    }

    void AncestorPathReturnsSingleIdForTopLevelNode() {
        FolderTreeModel model;

        FolderNode root;
        root.id = 0;
        root.name = "Root";

        FolderNode a;
        a.id = 10;
        a.name = "A";
        root.children.append(a);

        model.SetRoot(root);

        auto path = model.ancestorPath(10);
        QCOMPARE(path.size(), 1);
        QCOMPARE(path[0], quint64(10));
    }

    void AncestorPathReturnsFullChainForDeeplyNestedNode() {
        FolderTreeModel model;

        FolderNode root;
        root.id = 0;
        root.name = "Root";

        FolderNode docs;
        docs.id = 1;
        docs.name = "Docs";

        FolderNode work;
        work.id = 2;
        work.name = "Work";

        FolderNode project;
        project.id = 3;
        project.name = "Project";
        work.children.append(project);

        docs.children.append(work);
        root.children.append(docs);
        model.SetRoot(root);

        auto path = model.ancestorPath(3);
        QCOMPARE(path.size(), 3);
        QCOMPARE(path[0], quint64(1));
        QCOMPARE(path[1], quint64(2));
        QCOMPARE(path[2], quint64(3));
    }

    void AncestorPathExcludesVirtualRoot() {
        FolderTreeModel model;

        FolderNode root;
        root.id = 0;
        root.name = "My Drive";

        FolderNode child;
        child.id = 42;
        child.name = "Child";
        root.children.append(child);

        model.SetRoot(root);

        auto path = model.ancestorPath(42);
        QCOMPARE(path.size(), 1);
        QCOMPARE(path[0], quint64(42));
    }

    void IsAncestorReturnsTrueForDirectParent() {
        FolderTreeModel model;

        FolderNode root;
        root.id = 0;
        root.name = "Root";

        FolderNode parent;
        parent.id = 10;
        parent.name = "Parent";

        FolderNode child;
        child.id = 20;
        child.name = "Child";
        parent.children.append(child);

        root.children.append(parent);
        model.SetRoot(root);

        QVERIFY(model.isAncestor(10, 20));
    }

    void IsAncestorReturnsTrueForGrandparent() {
        FolderTreeModel model;

        FolderNode root;
        root.id = 0;
        root.name = "Root";

        FolderNode docs;
        docs.id = 1;
        docs.name = "Docs";

        FolderNode work;
        work.id = 2;
        work.name = "Work";

        FolderNode project;
        project.id = 3;
        project.name = "Project";
        work.children.append(project);

        docs.children.append(work);
        root.children.append(docs);
        model.SetRoot(root);

        QVERIFY(model.isAncestor(1, 3));
        QVERIFY(model.isAncestor(2, 3));
    }

    void IsAncestorReturnsFalseForUnrelatedNodes() {
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

        QVERIFY(!model.isAncestor(10, 20));
        QVERIFY(!model.isAncestor(20, 10));
    }

    void IsAncestorReturnsFalseForSameNode() {
        FolderTreeModel model;

        FolderNode root;
        root.id = 0;
        root.name = "Root";

        FolderNode a;
        a.id = 10;
        a.name = "A";
        root.children.append(a);

        model.SetRoot(root);

        QVERIFY(!model.isAncestor(10, 10));
    }

    void IsAncestorReturnsFalseForMissingDescendant() {
        FolderTreeModel model;

        FolderNode root;
        root.id = 0;
        root.name = "Root";
        model.SetRoot(root);

        QVERIFY(!model.isAncestor(0, 999));
    }

    void IndexOfFindsDeeplyNestedNode() {
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

        FolderNode c;
        c.id = 30;
        c.name = "C";
        b.children.append(c);

        a.children.append(b);
        root.children.append(a);
        model.SetRoot(root);

        auto idx = model.indexOf(30);
        QVERIFY(idx.isValid());
        QCOMPARE(model.data(idx, FolderTreeModel::IdRole).toULongLong(), quint64(30));
        QCOMPARE(model.data(idx, FolderTreeModel::NameRole).toString(), QString("C"));
        QCOMPARE(model.data(idx, FolderTreeModel::DepthRole).toInt(), 2);

        auto parent = model.parent(idx);
        QVERIFY(parent.isValid());
        QCOMPARE(model.data(parent, FolderTreeModel::IdRole).toULongLong(), quint64(20));
    }

    void IndexOfReturnsInvalidForEmptyModel() {
        FolderTreeModel model;
        auto idx = model.indexOf(1);
        QVERIFY(!idx.isValid());
    }

    void IndexOfStableAfterSetRootReload() {
        FolderTreeModel model;

        FolderNode first;
        first.id = 0;
        first.name = "First";

        FolderNode a;
        a.id = 10;
        a.name = "A";

        FolderNode b;
        b.id = 20;
        b.name = "B";
        a.children.append(b);

        first.children.append(a);
        model.SetRoot(first);

        QVERIFY(model.indexOf(20).isValid());

        FolderNode second;
        second.id = 0;
        second.name = "Second";

        FolderNode a2;
        a2.id = 10;
        a2.name = "A2";

        FolderNode b2;
        b2.id = 20;
        b2.name = "B2";
        a2.children.append(b2);

        second.children.append(a2);
        model.SetRoot(second);

        auto idx_after = model.indexOf(20);
        QVERIFY(idx_after.isValid());
        QCOMPARE(model.data(idx_after, FolderTreeModel::NameRole).toString(), QString("B2"));
        QCOMPARE(model.data(idx_after, FolderTreeModel::DepthRole).toInt(), 1);
    }

    void OldIndexesMustNotBeUsedAfterSetRootReload() {
        FolderTreeModel model;

        FolderNode first;
        first.id = 0;
        first.name = "First";

        FolderNode a;
        a.id = 10;
        a.name = "A";
        first.children.append(a);
        model.SetRoot(first);

        model.SetRoot(FolderNode{});

        QCOMPARE(model.rowCount(), 0);

        auto fresh_idx = model.indexOf(10);
        QVERIFY(!fresh_idx.isValid());
    }

    void SetRootEmitsModelReset() {
        FolderTreeModel model;

        QSignalSpy about_to_reset(&model, &QAbstractItemModel::modelAboutToBeReset);
        QSignalSpy reset_done(&model, &QAbstractItemModel::modelReset);

        FolderNode root;
        root.id = 0;
        root.name = "Root";
        model.SetRoot(root);

        QCOMPARE(about_to_reset.count(), 1);
        QCOMPARE(reset_done.count(), 1);
    }

    void SetRootMultipleReloadsPreserveLookup() {
        FolderTreeModel model;

        for (int i = 0; i < 5; ++i) {
            FolderNode root;
            root.id = 0;
            root.name = "Root";

            FolderNode child;
            child.id = 100;
            child.name = "Child";
            root.children.append(child);

            model.SetRoot(root);

            auto idx = model.indexOf(100);
            QVERIFY(idx.isValid());
            QCOMPARE(model.data(idx, FolderTreeModel::IdRole).toULongLong(), quint64(100));
            QCOMPARE(model.data(idx, FolderTreeModel::DepthRole).toInt(), 0);
            QVERIFY(model.hasChildren());
        }
    }

    void SetRootWithEmptyRootHasNoTopLevelChildren() {
        FolderTreeModel model;

        FolderNode emptyRoot;
        emptyRoot.id = 0;
        emptyRoot.name = "Root";
        model.SetRoot(emptyRoot);

        QCOMPARE(model.rowCount(), 0);
        QVERIFY(!model.hasChildren());
    }

    void SetRootAlternatingEmptyAndPopulated() {
        FolderTreeModel model;

        // Start empty
        FolderNode empty;
        empty.id = 0;
        empty.name = "Empty";
        model.SetRoot(empty);
        QCOMPARE(model.rowCount(), 0);
        QVERIFY(!model.hasChildren());
        QVERIFY(!model.indexOf(10).isValid());
        QVERIFY(model.ancestorPath(10).isEmpty());
        QVERIFY(!model.isAncestor(10, 20));

        // Populate
        FolderNode populated;
        populated.id = 0;
        populated.name = "Populated";
        FolderNode a;
        a.id = 10;
        a.name = "A";
        FolderNode b;
        b.id = 20;
        b.name = "B";
        a.children.append(b);
        populated.children.append(a);
        model.SetRoot(populated);

        QCOMPARE(model.rowCount(), 1);
        QVERIFY(model.hasChildren());
        QVERIFY(model.indexOf(20).isValid());
        QCOMPARE(model.ancestorPath(20).size(), 2);
        QVERIFY(model.isAncestor(10, 20));

        // Clear again
        model.SetRoot(empty);
        QCOMPARE(model.rowCount(), 0);
        QVERIFY(!model.hasChildren());
        QVERIFY(!model.indexOf(10).isValid());
        QVERIFY(model.ancestorPath(10).isEmpty());
        QVERIFY(!model.isAncestor(10, 20));
    }

    void AncestorPathReturnsEmptyForEmptyModel() {
        FolderTreeModel model;
        QVERIFY(model.ancestorPath(1).isEmpty());
        QVERIFY(model.ancestorPath(0).isEmpty());
    }

    void IsAncestorReturnsFalseForEmptyModel() {
        FolderTreeModel model;
        QVERIFY(!model.isAncestor(0, 1));
        QVERIFY(!model.isAncestor(1, 2));
    }

    void HasChildrenReturnsFalseAfterClearingTree() {
        FolderTreeModel model;

        FolderNode root;
        root.id = 0;
        root.name = "Root";
        FolderNode child;
        child.id = 10;
        child.name = "Child";
        root.children.append(child);
        model.SetRoot(root);

        QVERIFY(model.hasChildren());

        model.SetRoot(FolderNode{});
        QVERIFY(!model.hasChildren());
        QCOMPARE(model.rowCount(), 0);
    }

    void GetNodeReturnsCorrectNodeForNestedIndex() {
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

        auto docs_idx = model.index(0, 0);
        auto* docs_node = model.GetNode(docs_idx);
        QVERIFY(docs_node != nullptr);
        QCOMPARE(docs_node->id, quint64(1));
        QCOMPARE(docs_node->children.size(), 1);

        auto sub_idx = model.index(0, 0, docs_idx);
        auto* sub_node = model.GetNode(sub_idx);
        QVERIFY(sub_node != nullptr);
        QCOMPARE(sub_node->id, quint64(2));
        QVERIFY(sub_node->children.isEmpty());
    }

    void ParentRoundTripAtMultipleDepths() {
        FolderTreeModel model;

        FolderNode root;
        root.id = 0;
        root.name = "Root";

        FolderNode l1;
        l1.id = 10;
        l1.name = "L1";

        FolderNode l2;
        l2.id = 20;
        l2.name = "L2";

        FolderNode l3;
        l3.id = 30;
        l3.name = "L3";

        l2.children.append(l3);
        l1.children.append(l2);
        root.children.append(l1);
        model.SetRoot(root);

        auto l1_idx = model.index(0, 0);
        auto l2_idx = model.index(0, 0, l1_idx);
        auto l3_idx = model.index(0, 0, l2_idx);

        QVERIFY(l3_idx.isValid());

        auto l3_parent = model.parent(l3_idx);
        QVERIFY(l3_parent.isValid());
        QCOMPARE(model.data(l3_parent, FolderTreeModel::IdRole).toULongLong(), quint64(20));

        auto l2_parent = model.parent(l3_parent);
        QVERIFY(l2_parent.isValid());
        QCOMPARE(model.data(l2_parent, FolderTreeModel::IdRole).toULongLong(), quint64(10));

        auto l1_parent = model.parent(l2_parent);
        QVERIFY(!l1_parent.isValid());
    }
};

int run_TestFolderTreeModel(int argc, char* argv[]) {
    TestFolderTreeModel test;
    return QTest::qExec(&test, argc, argv);
}

#include "test_folder_tree_model.moc"
