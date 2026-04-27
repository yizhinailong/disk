#include <QtQuickTest/quicktest.h>
#include <QQmlEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <qqml.h>

#include "models/DriveListModel.hpp"
#include "models/FolderTreeModel.hpp"

// ── Minimal stub objects matching the auth context property API ────────────
// These are injected as QML context properties so that runtime-instantiated
// auth pages (LoginPage, RegisterPage, AuthShell) do not produce
// "ReferenceError: authService is not defined" warnings.

class StubAuthService : public QObject {
    Q_OBJECT
public:
    explicit StubAuthService(QObject *parent = nullptr) : QObject(parent) {}
    Q_INVOKABLE void Login(const QString &, const QString &) {}
    Q_INVOKABLE void Register(const QString &, const QString &, const QString &) {}
signals:
    void loginSuccess(const QVariant &, const QVariant &, const QVariant &, const QVariant &);
    void loginFailure(const QVariant &, const QVariant &);
    void registerSuccess(const QVariant &);
    void registerFailure(const QVariant &, const QVariant &);
};

class StubShellController : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString pageState READ pageState NOTIFY pageStateChanged)
public:
    explicit StubShellController(QObject *parent = nullptr)
        : QObject(parent), m_pageState("loading") {}
    Q_INVOKABLE void navigateToLogin() {}
    Q_INVOKABLE void navigateToRegister() {}
    Q_INVOKABLE void setPageState(const QString &state) {
        if (m_pageState != state) {
            m_pageState = state;
            emit pageStateChanged();
        }
    }
    QString pageState() const { return m_pageState; }
signals:
    void pageStateChanged();
private:
    QString m_pageState;
};

class StubOwnerSession : public QObject {
    Q_OBJECT
public:
    explicit StubOwnerSession(QObject *parent = nullptr) : QObject(parent) {}
    Q_INVOKABLE void StartLogin() {}
};

class StubSessionStore : public QObject {
    Q_OBJECT
    Q_PROPERTY(StubOwnerSession *owner READ owner CONSTANT)
public:
    explicit StubSessionStore(QObject *parent = nullptr)
        : QObject(parent), m_owner(new StubOwnerSession(this)) {}
    StubOwnerSession *owner() const { return m_owner; }
private:
    StubOwnerSession *m_owner;
};

class FolderTreeTestHarness : public QObject {
    Q_OBJECT
    Q_PROPERTY(disk::desktop::FolderTreeModel *treeModel READ treeModel CONSTANT)
public:
    explicit FolderTreeTestHarness(QObject *parent = nullptr)
        : QObject(parent), m_treeModel(new disk::desktop::FolderTreeModel(this)) {
        LoadNavigationTree();
    }

    disk::desktop::FolderTreeModel *treeModel() const { return m_treeModel; }

    Q_INVOKABLE void LoadNavigationTree() {
        disk::desktop::FolderNode root;
        root.id = 0;
        root.name = QStringLiteral("Root");

        disk::desktop::FolderNode documents;
        documents.id = 10;
        documents.name = QStringLiteral("Documents");

        disk::desktop::FolderNode project;
        project.id = 20;
        project.name = QStringLiteral("Project Alpha");

        disk::desktop::FolderNode releaseNotes;
        releaseNotes.id = 30;
        releaseNotes.name = QStringLiteral("Release Notes 2026");

        disk::desktop::FolderNode photos;
        photos.id = 40;
        photos.name = QStringLiteral("Photos");

        project.children.append(releaseNotes);
        documents.children.append(project);
        documents.children.append(photos);

        disk::desktop::FolderNode archive;
        archive.id = 50;
        archive.name = QStringLiteral("Archive");

        disk::desktop::FolderNode planning;
        planning.id = 60;
        planning.name = QStringLiteral("Quarterly Planning Artifacts and Long Folder Names");

        disk::desktop::FolderNode planningChild;
        planningChild.id = 61;
        planningChild.name = QStringLiteral("Meeting Notes");

        planning.children.append(planningChild);

        root.children = { documents, archive, planning };
        m_treeModel->SetRoot(root);
    }

    Q_INVOKABLE void LoadLongNameTree() {
        disk::desktop::FolderNode root;
        root.id = 0;
        root.name = QStringLiteral("Root");

        disk::desktop::FolderNode longBranch;
        longBranch.id = 100;
        longBranch.name = QStringLiteral("Quarterly Planning Artifacts and Documentation Review Collection");

        disk::desktop::FolderNode child;
        child.id = 110;
        child.name = QStringLiteral("Subfolder");

        longBranch.children.append(child);
        root.children = { longBranch };
        m_treeModel->SetRoot(root);
    }

    Q_INVOKABLE void LoadEmptyTree() {
        disk::desktop::FolderNode root;
        root.id = 0;
        root.name = QStringLiteral("Root");
        m_treeModel->SetRoot(root);
    }

private:
    disk::desktop::FolderTreeModel *m_treeModel;
};

class StubDriveManager : public QObject {
    Q_OBJECT
    Q_PROPERTY(disk::desktop::DriveListModel *listModel READ listModel CONSTANT)
    Q_PROPERTY(disk::desktop::FolderTreeModel *treeModel READ treeModel CONSTANT)
public:
    explicit StubDriveManager(QObject *parent = nullptr)
        : QObject(parent),
          m_listModel(new disk::desktop::DriveListModel(this)),
          m_treeModel(new disk::desktop::FolderTreeModel(this)) {}

    disk::desktop::DriveListModel *listModel() const { return m_listModel; }
    disk::desktop::FolderTreeModel *treeModel() const { return m_treeModel; }

    Q_INVOKABLE void listFiles(const QString &parentId, int page = 1, int pageSize = 50,
                                const QString &sort = "name_asc", const QString &typeFilter = "") {
        m_listFilesCalls.append(parentId);
    }
    Q_INVOKABLE void searchFiles(const QString &) {}
    Q_INVOKABLE void getFileDetail(const QString &) {}
    Q_INVOKABLE void createFolder(const QString &parentId, const QString &name) {
        m_createFolderCalls.append({parentId, name});
    }
    Q_INVOKABLE void renameItem(const QString &fileId, const QString &newName) {
        m_renameItemCalls.append({fileId, newName});
    }
    Q_INVOKABLE void moveItems(const QStringList &, const QString &) {}
    Q_INVOKABLE void copyItems(const QStringList &, const QString &) {}
    Q_INVOKABLE void deleteItems(const QStringList &fileIds) {
        m_deleteItemsCalls.append(fileIds);
    }
    Q_INVOKABLE void loadFolderTree() {
        m_loadFolderTreeCallCount++;
    }
    Q_INVOKABLE void loadBreadcrumb(const QString &folderId) {
        m_loadBreadcrumbCalls.append(folderId);
    }

    Q_INVOKABLE int loadFolderTreeCallCount() const { return m_loadFolderTreeCallCount; }
    Q_INVOKABLE QStringList listFilesCalls() const { return m_listFilesCalls; }
    Q_INVOKABLE QStringList loadBreadcrumbCalls() const { return m_loadBreadcrumbCalls; }
    Q_INVOKABLE QList<QStringList> createFolderCalls() const { return m_createFolderCalls; }
    Q_INVOKABLE QList<QStringList> renameItemCalls() const { return m_renameItemCalls; }
    Q_INVOKABLE QList<QStringList> deleteItemsCalls() const { return m_deleteItemsCalls; }

    Q_INVOKABLE void resetCounts() {
        m_loadFolderTreeCallCount = 0;
        m_listFilesCalls.clear();
        m_loadBreadcrumbCalls.clear();
        m_createFolderCalls.clear();
        m_renameItemCalls.clear();
        m_deleteItemsCalls.clear();
    }

    Q_INVOKABLE void clearTreeModel() {
        m_treeModel->SetRoot(disk::desktop::FolderNode{});
    }

    Q_INVOKABLE void populateTreeModel() {
        disk::desktop::FolderNode root;
        root.id = 0;
        root.name = QStringLiteral("Root");

        disk::desktop::FolderNode docs;
        docs.id = 10;
        docs.name = QStringLiteral("Documents");

        disk::desktop::FolderNode sub;
        sub.id = 20;
        sub.name = QStringLiteral("Projects");
        docs.children.append(sub);

        root.children.append(docs);
        m_treeModel->SetRoot(root);
    }

    Q_INVOKABLE void addListFileItem(quint64 id, const QString &kind, const QString &name) {
        disk::desktop::DriveItem item;
        item.id = id;
        item.kind = kind;
        item.name = name;
        m_listModel->AddItem(item);
    }

    Q_INVOKABLE void clearListModel() {
        m_listModel->Clear();
    }

signals:
    void apiError(const QString &message, int code);
    void fileDetailLoaded(const QVariantMap &detail);
    void breadcrumbLoaded(const QVariantList &breadcrumb);
    void operationSuccess(const QString &message);
    void paginationLoaded(int page, int totalPages, int total);
    void listLoadFailed(const QString &message, int code);
    void treeLoaded();

private:
    disk::desktop::DriveListModel *m_listModel;
    disk::desktop::FolderTreeModel *m_treeModel;
    int m_loadFolderTreeCallCount = 0;
    QStringList m_listFilesCalls;
    QStringList m_loadBreadcrumbCalls;
    QList<QStringList> m_createFolderCalls;
    QList<QStringList> m_renameItemCalls;
    QList<QStringList> m_deleteItemsCalls;
};

class StubUploadModel : public QAbstractListModel {
    Q_OBJECT
public:
    explicit StubUploadModel(QObject *parent = nullptr) : QAbstractListModel(parent) {}
    int rowCount(const QModelIndex &) const override { return 0; }
    QVariant data(const QModelIndex &, int) const override { return {}; }
};

class StubDownloadModel : public QAbstractListModel {
    Q_OBJECT
public:
    explicit StubDownloadModel(QObject *parent = nullptr) : QAbstractListModel(parent) {}
    int rowCount(const QModelIndex &) const override { return 0; }
    QVariant data(const QModelIndex &, int) const override { return {}; }
};

class StubTransferManager : public QObject {
    Q_OBJECT
    Q_PROPERTY(StubUploadModel *uploadModel READ uploadModel CONSTANT)
    Q_PROPERTY(StubDownloadModel *downloadModel READ downloadModel CONSTANT)
public:
    explicit StubTransferManager(QObject *parent = nullptr)
        : QObject(parent),
          m_uploadModel(new StubUploadModel(this)),
          m_downloadModel(new StubDownloadModel(this)) {}

    StubUploadModel *uploadModel() const { return m_uploadModel; }
    StubDownloadModel *downloadModel() const { return m_downloadModel; }

    Q_INVOKABLE void StartUpload(const QString &, quint64) {}
    Q_INVOKABLE void StartDownload(quint64, const QString &, const QString &) {}

private:
    StubUploadModel *m_uploadModel;
    StubDownloadModel *m_downloadModel;
};

class QuickTestSetup : public QObject {
    Q_OBJECT
public:
    explicit QuickTestSetup(QObject *parent = nullptr) : QObject(parent) {
        m_authService = new StubAuthService(this);
        m_shellController = new StubShellController(this);
        m_sessionStore = new StubSessionStore(this);
        m_folderTreeTestHarness = new FolderTreeTestHarness(this);
        m_driveManager = new StubDriveManager(this);
        m_transferManager = new StubTransferManager(this);
    }

    Q_INVOKABLE void qmlEngineAvailable(QQmlEngine *engine) {
        auto ctx = engine->rootContext();
        ctx->setContextProperty("authService", m_authService);
        ctx->setContextProperty("shellController", m_shellController);
        ctx->setContextProperty("sessionStore", m_sessionStore);
        ctx->setContextProperty("folderTreeTestHarness", m_folderTreeTestHarness);
        ctx->setContextProperty("driveManager", m_driveManager);
        ctx->setContextProperty("transferManager", m_transferManager);
    }

private:
    StubAuthService *m_authService;
    StubShellController *m_shellController;
    StubSessionStore *m_sessionStore;
    FolderTreeTestHarness *m_folderTreeTestHarness;
    StubDriveManager *m_driveManager;
    StubTransferManager *m_transferManager;
};

int main(int argc, char **argv) {
    QQuickStyle::setStyle(QStringLiteral("Basic"));
    qmlRegisterType<FolderTreeTestHarness>("QuickTestSupport", 1, 0, "FolderTreeTestHarness");

    QTEST_SET_MAIN_SOURCE_PATH
    static QuickTestSetup setup;
    return quick_test_main_with_setup(argc, argv, "desktop-quick-tests",
                                      QUICK_TEST_SOURCE_DIR, &setup);
}

#include "quick_test_main.moc"
