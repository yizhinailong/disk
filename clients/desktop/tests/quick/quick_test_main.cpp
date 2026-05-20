#include <QtQuickTest/quicktest.h>
#include <QQmlEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickItemGrabResult>
#include <QQuickItem>
#include <QImage>
#include <QDir>
#include <QAbstractListModel>
#include <QStandardPaths>
#include <QVariantMap>
#include <qqml.h>

#include "models/DriveListModel.hpp"
#include "models/FolderTreeModel.hpp"

// ── Minimal stub objects matching the context property API ──────────────
// These are injected as QML context properties so that runtime-instantiated
// pages and shells do not produce "ReferenceError: X is not defined" warnings.
// Each stub exposes the minimum Q_PROPERTY / Q_INVOKABLE surface needed by
// the QML pages that reference the corresponding production manager.

class StubAuthService : public QObject {
    Q_OBJECT
public:
    explicit StubAuthService(QObject *parent = nullptr) : QObject(parent) {}
    Q_INVOKABLE void Login(const QString &, const QString &) {}
    Q_INVOKABLE void Register(const QString &, const QString &, const QString &) {}
    Q_INVOKABLE void AccessShare(const QString &, const QString &) {}
signals:
    void loginSuccess(const QVariant &, const QVariant &, const QVariant &, const QVariant &);
    void loginFailure(const QVariant &, const QVariant &);
    void registerSuccess(const QVariant &);
    void registerFailure(const QVariant &, const QVariant &);
    void shareAccessSuccess(const QVariant &, const QVariant &, const QVariant &, const QVariant &);
    void shareAccessFailure(const QVariant &, const QVariant &);
};

class StubShellController : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString currentShell READ currentShell NOTIFY currentShellChanged)
    Q_PROPERTY(QString pageState READ pageState NOTIFY pageStateChanged)
public:
    explicit StubShellController(QObject *parent = nullptr)
        : QObject(parent), m_currentShell("login"), m_pageState("loading") {}

    QString currentShell() const { return m_currentShell; }
    QString pageState() const { return m_pageState; }

    Q_INVOKABLE void navigateToLogin() { setCurrentShell("login"); }
    Q_INVOKABLE void navigateToRegister() { setCurrentShell("register"); }
    Q_INVOKABLE void navigateToOwner() { setCurrentShell("owner"); }
    Q_INVOKABLE void navigateToVisitor(const QString &shareId) {
        Q_UNUSED(shareId);
        setCurrentShell("visitor");
    }
    Q_INVOKABLE void navigateToSplash() { setCurrentShell("splash"); }

    Q_INVOKABLE void setPageState(const QString &state) {
        if (m_pageState != state) {
            m_pageState = state;
            emit pageStateChanged();
        }
    }

    // Test seam: allows tests to drive shell transitions directly.
    Q_INVOKABLE void setCurrentShell(const QString &shell) {
        if (m_currentShell != shell) {
            m_currentShell = shell;
            emit currentShellChanged();
        }
    }

signals:
    void currentShellChanged();
    void pageStateChanged();

private:
    QString m_currentShell;
    QString m_pageState;
};

class StubOwnerSession : public QObject {
    Q_OBJECT
public:
    explicit StubOwnerSession(QObject *parent = nullptr) : QObject(parent) {}
    Q_INVOKABLE void StartLogin() {}
};

class StubVisitorSession : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString shareId READ shareId NOTIFY shareIdChanged)
    Q_PROPERTY(QString state READ state NOTIFY stateChanged)
public:
    explicit StubVisitorSession(QObject *parent = nullptr)
        : QObject(parent), m_shareId(""), m_state("idle") {}

    QString shareId() const { return m_shareId; }
    QString state() const { return m_state; }

    Q_INVOKABLE void OpenShare(const QString &shareId) {
        m_shareId = shareId;
        m_state = "unverified";
        emit shareIdChanged();
        emit stateChanged();
    }

    Q_INVOKABLE void StartVerify(const QString &password = {}) {
        Q_UNUSED(password);
        m_state = "verifying";
        emit stateChanged();
    }

    Q_INVOKABLE void HandleVerifySuccess(const QString &shareToken, int expiresIn,
                                          const QString &permission, const QVariant &files) {
        Q_UNUSED(shareToken);
        Q_UNUSED(expiresIn);
        Q_UNUSED(permission);
        Q_UNUSED(files);
        m_state = "active";
        emit stateChanged();
    }

signals:
    void shareIdChanged();
    void stateChanged();
    void verifyRequested(const QString &shareId, const QString &password);

private:
    QString m_shareId;
    QString m_state;
};

class StubSessionStore : public QObject {
    Q_OBJECT
    Q_PROPERTY(StubOwnerSession *owner READ owner CONSTANT)
    Q_PROPERTY(StubVisitorSession *visitor READ visitor CONSTANT)
public:
    explicit StubSessionStore(QObject *parent = nullptr)
        : QObject(parent),
          m_owner(new StubOwnerSession(this)),
          m_visitor(new StubVisitorSession(this)) {}
    StubOwnerSession *owner() const { return m_owner; }
    StubVisitorSession *visitor() const { return m_visitor; }

    Q_INVOKABLE void ActivateVisitor(const QString &shareId) {
        m_visitor->OpenShare(shareId);
    }

private:
    StubOwnerSession *m_owner;
    StubVisitorSession *m_visitor;
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
        Q_UNUSED(page);
        Q_UNUSED(pageSize);
        Q_UNUSED(typeFilter);
        m_listFilesCalls.append(parentId);
        m_listFilesSortCalls.append(sort);
    }
    Q_INVOKABLE void searchFiles(const QString &query) {
        m_searchFilesCalls.append(query);
    }
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
        m_deleteDriveItemsCalls.append({fileIds, {}});
    }
    Q_INVOKABLE void deleteDriveItems(const QStringList &fileIds, const QStringList &folderIds) {
        m_deleteDriveItemsCalls.append({fileIds, folderIds});
    }
    Q_INVOKABLE void loadFolderTree() {
        m_loadFolderTreeCallCount++;
    }
    Q_INVOKABLE void loadBreadcrumb(const QString &folderId) {
        m_loadBreadcrumbCalls.append(folderId);
    }

    Q_INVOKABLE int loadFolderTreeCallCount() const { return m_loadFolderTreeCallCount; }
    Q_INVOKABLE QStringList listFilesCalls() const { return m_listFilesCalls; }
    Q_INVOKABLE QStringList listFilesSortCalls() const { return m_listFilesSortCalls; }
    Q_INVOKABLE QStringList searchFilesCalls() const { return m_searchFilesCalls; }
    Q_INVOKABLE QStringList loadBreadcrumbCalls() const { return m_loadBreadcrumbCalls; }
    Q_INVOKABLE QList<QStringList> createFolderCalls() const { return m_createFolderCalls; }
    Q_INVOKABLE QList<QStringList> renameItemCalls() const { return m_renameItemCalls; }
    Q_INVOKABLE QList<QStringList> deleteItemsCalls() const { return m_deleteItemsCalls; }
    Q_INVOKABLE QList<QList<QStringList>> deleteDriveItemsCalls() const { return m_deleteDriveItemsCalls; }

    Q_INVOKABLE void resetCounts() {
        m_loadFolderTreeCallCount = 0;
        m_listFilesCalls.clear();
        m_listFilesSortCalls.clear();
        m_searchFilesCalls.clear();
        m_loadBreadcrumbCalls.clear();
        m_createFolderCalls.clear();
        m_renameItemCalls.clear();
        m_deleteItemsCalls.clear();
        m_deleteDriveItemsCalls.clear();
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
    QStringList m_listFilesSortCalls;
    QStringList m_searchFilesCalls;
    QStringList m_loadBreadcrumbCalls;
    QList<QStringList> m_createFolderCalls;
    QList<QStringList> m_renameItemCalls;
    QList<QStringList> m_deleteItemsCalls;
    QList<QList<QStringList>> m_deleteDriveItemsCalls;
};

class StubShareListModel : public QAbstractListModel {
    Q_OBJECT
public:
    explicit StubShareListModel(QObject *parent = nullptr) : QAbstractListModel(parent) {}

    enum Roles {
        ShareIdRole = Qt::UserRole + 1,
        ShareLinkRole,
        PermissionRole,
        HasPasswordRole,
        CreatedAtRole,
        ExpiresAtRole,
        StatusRole,
        ViewCountRole,
        DownloadCountRole,
        PrimaryItemNameRole,
        ItemCountRole,
        UpdatedAtRole,
    };

    int rowCount(const QModelIndex &parent = QModelIndex()) const override {
        return parent.isValid() ? 0 : m_items.size();
    }

    QVariant data(const QModelIndex &index, int role) const override {
        if (!index.isValid() || index.row() < 0 || index.row() >= m_items.size()) {
            return {};
        }

        const auto &item = m_items.at(index.row());
        switch (role) {
        case ShareIdRole: return item.value("shareId");
        case ShareLinkRole: return item.value("shareLink");
        case PermissionRole: return item.value("permission");
        case HasPasswordRole: return item.value("hasPassword");
        case CreatedAtRole: return item.value("createdAt");
        case ExpiresAtRole: return item.value("expiresAt");
        case StatusRole: return item.value("status");
        case ViewCountRole: return item.value("viewCount");
        case DownloadCountRole: return item.value("downloadCount");
        case PrimaryItemNameRole: return item.value("primaryItemName");
        case ItemCountRole: return item.value("itemCount");
        case UpdatedAtRole: return item.value("updatedAt");
        default: return {};
        }
    }

    QHash<int, QByteArray> roleNames() const override {
        return {
            {ShareIdRole, "shareId"},
            {ShareLinkRole, "shareLink"},
            {PermissionRole, "permission"},
            {HasPasswordRole, "hasPassword"},
            {CreatedAtRole, "createdAt"},
            {ExpiresAtRole, "expiresAt"},
            {StatusRole, "status"},
            {ViewCountRole, "viewCount"},
            {DownloadCountRole, "downloadCount"},
            {PrimaryItemNameRole, "primaryItemName"},
            {ItemCountRole, "itemCount"},
            {UpdatedAtRole, "updatedAt"},
        };
    }

    Q_INVOKABLE void addItem(const QString &shareId, const QString &primaryItemName,
                             const QString &permission = QStringLiteral("download"),
                             const QString &status = QStringLiteral("active"),
                             bool hasPassword = false, int viewCount = 0,
                             int downloadCount = 0, const QString &shareLink = {},
                             const QString &updatedAt = {}, const QString &expiresAt = {},
                             int itemCount = -1) {
        QVariantMap item;
        item["shareId"] = shareId;
        item["shareLink"] = shareLink;
        item["permission"] = permission;
        item["hasPassword"] = hasPassword;
        item["status"] = status;
        item["viewCount"] = viewCount;
        item["downloadCount"] = downloadCount;
        item["primaryItemName"] = primaryItemName;
        if (!updatedAt.isEmpty()) {
            item["updatedAt"] = updatedAt;
        }
        if (!expiresAt.isEmpty()) {
            item["expiresAt"] = expiresAt;
        }
        if (itemCount >= 0) {
            item["itemCount"] = itemCount;
        }

        beginInsertRows(QModelIndex(), m_items.size(), m_items.size());
        m_items.append(item);
        endInsertRows();
    }

    Q_INVOKABLE void clear() {
        if (m_items.isEmpty()) {
            return;
        }
        beginResetModel();
        m_items.clear();
        endResetModel();
    }

private:
    QVector<QVariantMap> m_items;
};

class StubTrashListModel : public QAbstractListModel {
    Q_OBJECT
public:
    explicit StubTrashListModel(QObject *parent = nullptr) : QAbstractListModel(parent) {}

    enum Roles {
        TrashIdRole = Qt::UserRole + 1,
        KindRole,
        OriginalIdRole,
        NameRole,
        SizeRole,
        OriginalPathRole,
        DeletedAtRole,
        ExpiresAtRole,
    };

    int rowCount(const QModelIndex &parent = QModelIndex()) const override {
        return parent.isValid() ? 0 : m_items.size();
    }

    QVariant data(const QModelIndex &index, int role) const override {
        if (!index.isValid() || index.row() < 0 || index.row() >= m_items.size()) {
            return {};
        }

        const auto &item = m_items.at(index.row());
        switch (role) {
        case TrashIdRole: return item.value("trashId");
        case KindRole: return item.value("kind");
        case OriginalIdRole: return item.value("originalId");
        case NameRole: return item.value("name");
        case SizeRole: return item.value("size");
        case OriginalPathRole: return item.value("originalPath");
        case DeletedAtRole: return item.value("deletedAt");
        case ExpiresAtRole: return item.value("expiresAt");
        default: return {};
        }
    }

    QHash<int, QByteArray> roleNames() const override {
        return {
            {TrashIdRole, "trashId"},
            {KindRole, "kind"},
            {OriginalIdRole, "originalId"},
            {NameRole, "name"},
            {SizeRole, "size"},
            {OriginalPathRole, "originalPath"},
            {DeletedAtRole, "deletedAt"},
            {ExpiresAtRole, "expiresAt"},
        };
    }

    Q_INVOKABLE void addItem(const QString &trashId, const QString &kind, const QString &name,
                             quint64 size = 0, const QString &originalPath = {},
                             const QString &deletedAt = {}, const QString &expiresAt = {},
                             const QString &originalId = {}) {
        QVariantMap item;
        item["trashId"] = trashId;
        item["kind"] = kind;
        item["name"] = name;
        item["size"] = QVariant::fromValue(size);
        item["originalPath"] = originalPath;
        if (!deletedAt.isEmpty()) {
            item["deletedAt"] = deletedAt;
        }
        if (!expiresAt.isEmpty()) {
            item["expiresAt"] = expiresAt;
        }
        if (!originalId.isEmpty()) {
            item["originalId"] = originalId;
        }

        beginInsertRows(QModelIndex(), m_items.size(), m_items.size());
        m_items.append(item);
        endInsertRows();
    }

    Q_INVOKABLE void clear() {
        if (m_items.isEmpty()) {
            return;
        }
        beginResetModel();
        m_items.clear();
        endResetModel();
    }

private:
    QVector<QVariantMap> m_items;
};

class StubBatchResultModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(QString operation READ operation NOTIFY operationChanged)
    Q_PROPERTY(int totalCount READ totalCount NOTIFY totalCountChanged)
    Q_PROPERTY(int successCount READ successCount NOTIFY successCountChanged)
    Q_PROPERTY(int failureCount READ failureCount NOTIFY failureCountChanged)
public:
    explicit StubBatchResultModel(QObject *parent = nullptr)
        : QAbstractListModel(parent) {}

    QString operation() const { return m_operation; }
    int totalCount() const { return m_totalCount; }
    int successCount() const { return m_successCount; }
    int failureCount() const { return m_failureCount; }

    enum Roles {
        ResourceKeyRole = Qt::UserRole + 1,
        StatusRole,
        RestoredItemIdRole,
        RestoredItemKindRole,
        ResolvedPathRole,
        FreedSpaceRole,
        ErrorRole,
    };

    int rowCount(const QModelIndex &parent = QModelIndex()) const override {
        return parent.isValid() ? 0 : m_items.size();
    }

    QVariant data(const QModelIndex &index, int role) const override {
        if (!index.isValid() || index.row() < 0 || index.row() >= m_items.size()) {
            return {};
        }

        const auto &item = m_items.at(index.row());
        switch (role) {
        case ResourceKeyRole: return item.value("resourceKey");
        case StatusRole: return item.value("status");
        case RestoredItemIdRole: return item.value("restoredItemId");
        case RestoredItemKindRole: return item.value("restoredItemKind");
        case ResolvedPathRole: return item.value("resolvedPath");
        case FreedSpaceRole: return item.value("freedSpace");
        case ErrorRole: return item.value("error");
        default: return {};
        }
    }

    QHash<int, QByteArray> roleNames() const override {
        return {
            {ResourceKeyRole, "resourceKey"},
            {StatusRole, "status"},
            {RestoredItemIdRole, "restoredItemId"},
            {RestoredItemKindRole, "restoredItemKind"},
            {ResolvedPathRole, "resolvedPath"},
            {FreedSpaceRole, "freedSpace"},
            {ErrorRole, "error"},
        };
    }

    Q_INVOKABLE void setResults(const QString &op, int total, int success, int failure) {
        m_operation = op;
        m_totalCount = total;
        m_successCount = success;
        m_failureCount = failure;
        emit operationChanged();
        emit totalCountChanged();
        emit successCountChanged();
        emit failureCountChanged();
    }

    Q_INVOKABLE void addEntry(const QString &resourceKey, const QString &status,
                              const QString &resolvedPath = {}, quint64 freedSpace = 0,
                              const QString &errorMessage = {}) {
        QVariantMap item;
        item["resourceKey"] = resourceKey;
        item["status"] = status;
        if (!resolvedPath.isEmpty()) {
            item["resolvedPath"] = resolvedPath;
        }
        if (freedSpace > 0) {
            item["freedSpace"] = QVariant::fromValue(freedSpace);
        }
        if (!errorMessage.isEmpty()) {
            QVariantMap error;
            error["message"] = errorMessage;
            item["error"] = error;
        }

        beginInsertRows(QModelIndex(), m_items.size(), m_items.size());
        m_items.append(item);
        endInsertRows();
    }

    Q_INVOKABLE void clear() {
        beginResetModel();
        m_items.clear();
        m_operation.clear();
        m_totalCount = 0;
        m_successCount = 0;
        m_failureCount = 0;
        emit operationChanged();
        emit totalCountChanged();
        emit successCountChanged();
        emit failureCountChanged();
        endResetModel();
    }

signals:
    void operationChanged();
    void totalCountChanged();
    void successCountChanged();
    void failureCountChanged();

private:
    QString m_operation;
    int m_totalCount = 0;
    int m_successCount = 0;
    int m_failureCount = 0;
    QVector<QVariantMap> m_items;
};

class StubProfileManager : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantMap userProfile READ userProfile NOTIFY userProfileChanged)
    Q_PROPERTY(QVariantMap storageStats READ storageStats NOTIFY storageStatsChanged)
public:
    explicit StubProfileManager(QObject *parent = nullptr) : QObject(parent) {
        m_userProfile["nickname"] = QStringLiteral("Test Owner");
        m_userProfile["username"] = QStringLiteral("testowner");
        m_storageStats["used"] = 0;
        m_storageStats["total"] = 1073741824;
        m_storageStats["quota"] = 1073741824;
    }

    QVariantMap userProfile() const { return m_userProfile; }
    QVariantMap storageStats() const { return m_storageStats; }

    Q_INVOKABLE void loadProfile() {}
    Q_INVOKABLE void updateProfile(const QString &, const QString &) {}
    Q_INVOKABLE void changePassword(const QString &, const QString &) {}
    Q_INVOKABLE void loadStorageStats() {}

    Q_INVOKABLE void setUserProfile(const QVariantMap &profile) {
        m_userProfile = profile;
        emit userProfileChanged();
    }
    Q_INVOKABLE void setStorageStats(const QVariantMap &stats) {
        m_storageStats = stats;
        emit storageStatsChanged();
    }

signals:
    void apiError(const QString &message, int code);
    void userProfileChanged();
    void storageStatsChanged();
    void operationSuccess(const QString &message);

private:
    QVariantMap m_userProfile;
    QVariantMap m_storageStats;
};

class StubShareManager : public QObject {
    Q_OBJECT
    Q_PROPERTY(StubShareListModel *listModel READ listModel CONSTANT)
    Q_PROPERTY(disk::desktop::DriveListModel *browseModel READ browseModel CONSTANT)
    Q_PROPERTY(StubBatchResultModel *batchResultModel READ batchResultModel CONSTANT)
public:
    explicit StubShareManager(QObject *parent = nullptr)
        : QObject(parent),
          m_listModel(new StubShareListModel(this)),
          m_browseModel(new disk::desktop::DriveListModel(this)),
          m_batchResultModel(new StubBatchResultModel(this)) {}

    StubShareListModel *listModel() const { return m_listModel; }
    disk::desktop::DriveListModel *browseModel() const { return m_browseModel; }
    StubBatchResultModel *batchResultModel() const { return m_batchResultModel; }

    Q_INVOKABLE void listShares(int page = 1, int pageSize = 20, const QString &status = "all") {
        Q_UNUSED(page);
        Q_UNUSED(pageSize);
        ++m_listSharesCallCount;
        m_listSharesStatuses.append(status);
    }
    Q_INVOKABLE void createShare(const QStringList &fileIds, const QStringList &folderIds,
                                 const QString &permission = "download",
                                 const QString &password = {}, int expireDays = 7) {
        QVariantMap call;
        call["fileIds"] = fileIds;
        call["folderIds"] = folderIds;
        call["permission"] = permission;
        call["password"] = password;
        call["expireDays"] = expireDays;
        m_createShareCalls.append(call);
    }
    Q_INVOKABLE void getShareDetail(const QString &) {}
    Q_INVOKABLE void updateShare(const QString &shareId, const QString &permission = {},
                                 const QString &password = {}, int expireDays = -1) {
        QVariantMap call;
        call["shareId"] = shareId;
        call["permission"] = permission;
        call["password"] = password;
        call["expireDays"] = expireDays;
        m_updateShareCalls.append(call);
    }
    Q_INVOKABLE void cancelShares(const QStringList &shareIds) {
        QVariantMap call;
        call["shareIds"] = shareIds;
        m_cancelSharesCalls.append(call);
    }
    Q_INVOKABLE void browseShare(const QString &, const QString & = {}) {}
    Q_INVOKABLE void saveShareItems(
        const QString &,
        const QStringList &,
        const QStringList &,
        const QString & = QStringLiteral("0")
    ) {}
    Q_INVOKABLE void getShareDetailVisitor(const QString &) {}

    Q_INVOKABLE int listSharesCallCount() const { return m_listSharesCallCount; }
    Q_INVOKABLE QStringList listSharesStatuses() const { return m_listSharesStatuses; }
    Q_INVOKABLE QVariantList createShareCalls() const { return m_createShareCalls; }
    Q_INVOKABLE QVariantList updateShareCalls() const { return m_updateShareCalls; }
    Q_INVOKABLE QVariantList cancelSharesCalls() const { return m_cancelSharesCalls; }
    Q_INVOKABLE void resetCounts() {
        m_listSharesCallCount = 0;
        m_listSharesStatuses.clear();
        m_createShareCalls.clear();
        m_updateShareCalls.clear();
        m_cancelSharesCalls.clear();
    }
    Q_INVOKABLE void addShareItem(const QString &shareId, const QString &primaryItemName,
                                  const QString &permission = QStringLiteral("download"),
                                  const QString &status = QStringLiteral("active"),
                                  bool hasPassword = false, int viewCount = 0,
                                  int downloadCount = 0, const QString &shareLink = {},
                                  const QString &updatedAt = {}, const QString &expiresAt = {},
                                  int itemCount = -1) {
        m_listModel->addItem(shareId, primaryItemName, permission, status, hasPassword,
                             viewCount, downloadCount, shareLink, updatedAt, expiresAt, itemCount);
    }
    Q_INVOKABLE void clearShareListModel() { m_listModel->clear(); }
    Q_INVOKABLE void clearBatchResultModel() { m_batchResultModel->clear(); }
    Q_INVOKABLE void addBatchResultEntry(const QString &resourceKey, const QString &status,
                                         const QString &resolvedPath = {}, quint64 freedSpace = 0,
                                         const QString &errorMessage = {}) {
        m_batchResultModel->addEntry(resourceKey, status, resolvedPath, freedSpace, errorMessage);
    }

signals:
    void apiError(const QString &message, int code);
    void operationSuccess(const QString &message);
    void shareDetailLoaded(const QVariantMap &detail);
    void paginationLoaded(int page, int totalPages, int total);
    void batchResultReady();
    void browseLoaded(const QString &shareId);
    void shareCreated(const QString &shareId, const QString &shareLink);

private:
    StubShareListModel *m_listModel;
    disk::desktop::DriveListModel *m_browseModel;
    StubBatchResultModel *m_batchResultModel;
    int m_listSharesCallCount = 0;
    QStringList m_listSharesStatuses;
    QVariantList m_createShareCalls;
    QVariantList m_updateShareCalls;
    QVariantList m_cancelSharesCalls;
};

class StubTrashManager : public QObject {
    Q_OBJECT
    Q_PROPERTY(StubTrashListModel *listModel READ listModel CONSTANT)
    Q_PROPERTY(StubBatchResultModel *batchResultModel READ batchResultModel CONSTANT)
public:
    explicit StubTrashManager(QObject *parent = nullptr)
        : QObject(parent),
          m_listModel(new StubTrashListModel(this)),
          m_batchResultModel(new StubBatchResultModel(this)) {}

    StubTrashListModel *listModel() const { return m_listModel; }
    StubBatchResultModel *batchResultModel() const { return m_batchResultModel; }

    Q_INVOKABLE void listTrash(int page = 1, int pageSize = 20) {
        Q_UNUSED(page);
        Q_UNUSED(pageSize);
        ++m_listTrashCallCount;
    }
    Q_INVOKABLE void restoreItems(const QStringList &trashIds) {
        m_restoreItemsCalls.append(trashIds);
    }
    Q_INVOKABLE void deleteItems(const QStringList &trashIds) {
        m_deleteItemsCalls.append(trashIds);
    }
    Q_INVOKABLE void clearAll() {
        ++m_clearAllCallCount;
    }

    Q_INVOKABLE int listTrashCallCount() const { return m_listTrashCallCount; }
    Q_INVOKABLE QList<QStringList> restoreItemsCalls() const { return m_restoreItemsCalls; }
    Q_INVOKABLE QList<QStringList> deleteItemsCalls() const { return m_deleteItemsCalls; }
    Q_INVOKABLE int clearAllCallCount() const { return m_clearAllCallCount; }
    Q_INVOKABLE void resetCounts() {
        m_listTrashCallCount = 0;
        m_restoreItemsCalls.clear();
        m_deleteItemsCalls.clear();
        m_clearAllCallCount = 0;
    }
    Q_INVOKABLE void addTrashItem(const QString &trashId, const QString &kind, const QString &name,
                                  quint64 size = 0, const QString &originalPath = {},
                                  const QString &deletedAt = {}, const QString &expiresAt = {},
                                  const QString &originalId = {}) {
        m_listModel->addItem(trashId, kind, name, size, originalPath, deletedAt, expiresAt, originalId);
    }
    Q_INVOKABLE void clearTrashListModel() { m_listModel->clear(); }
    Q_INVOKABLE void clearBatchResultModel() { m_batchResultModel->clear(); }
    Q_INVOKABLE void addBatchResultEntry(const QString &resourceKey, const QString &status,
                                         const QString &resolvedPath = {}, quint64 freedSpace = 0,
                                         const QString &errorMessage = {}) {
        m_batchResultModel->addEntry(resourceKey, status, resolvedPath, freedSpace, errorMessage);
    }

signals:
    void apiError(const QString &message, int code);
    void operationSuccess(const QString &message);
    void paginationLoaded(int page, int totalPages, int total);
    void batchResultReady();
    void clearAllCompleted(int deletedCount, quint64 freedSpace);

private:
    StubTrashListModel *m_listModel;
    StubBatchResultModel *m_batchResultModel;
    int m_listTrashCallCount = 0;
    QList<QStringList> m_restoreItemsCalls;
    QList<QStringList> m_deleteItemsCalls;
    int m_clearAllCallCount = 0;
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

// ── Screenshot helper for deterministic evidence capture ──────────────────
// Reads DESKTOP_QML_EVIDENCE_DIR from the environment.  If the variable is
// unset or the directory does not exist, every saveScreenshot() call returns
// false so that the QML test layer can fail deterministically.

class ScreenshotHelper : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool available READ available CONSTANT)
public:
    explicit ScreenshotHelper(QObject *parent = nullptr)
        : QObject(parent), m_valid(false) {
        auto envDir = qEnvironmentVariable("DESKTOP_QML_EVIDENCE_DIR");
        if (!envDir.isEmpty()) {
            QDir candidate(envDir);
            if (candidate.exists()) {
                m_dir = candidate;
                m_valid = true;
            }
        }
    }

    bool available() const { return m_valid; }

    Q_INVOKABLE bool saveScreenshot(QQuickItem *item, const QString &filename) {
        if (!item || !m_valid) {
            return false;
        }

        auto grab = item->grabToImage(QSize(item->width(), item->height()));
        if (!grab) {
            return false;
        }

        // Block until the grab completes (offscreen rendering is synchronous).
        QEventLoop loop;
        QObject::connect(grab.data(), &QQuickItemGrabResult::ready, &loop, &QEventLoop::quit);
        loop.exec();

        QString path = m_dir.absoluteFilePath(filename);
        if (!grab->image().save(path, "png")) {
            return false;
        }

        m_savedFiles.append(path);
        return true;
    }

    Q_INVOKABLE QStringList savedFiles() const { return m_savedFiles; }

    Q_INVOKABLE QString evidenceDir() const {
        return m_valid ? m_dir.absolutePath() : QString();
    }

private:
    QDir m_dir;
    bool m_valid;
    QStringList m_savedFiles;
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
        m_profileManager = new StubProfileManager(this);
        m_transferManager = new StubTransferManager(this);
        m_shareManager = new StubShareManager(this);
        m_trashManager = new StubTrashManager(this);
        m_screenshotHelper = new ScreenshotHelper(this);
    }

    Q_INVOKABLE void qmlEngineAvailable(QQmlEngine *engine) {
        auto ctx = engine->rootContext();
        ctx->setContextProperty("authService", m_authService);
        ctx->setContextProperty("shellController", m_shellController);
        ctx->setContextProperty("sessionStore", m_sessionStore);
        ctx->setContextProperty("folderTreeTestHarness", m_folderTreeTestHarness);
        ctx->setContextProperty("driveManager", m_driveManager);
        ctx->setContextProperty("profileManager", m_profileManager);
        ctx->setContextProperty("transferManager", m_transferManager);
        ctx->setContextProperty("shareManager", m_shareManager);
        ctx->setContextProperty("trashManager", m_trashManager);
        ctx->setContextProperty("screenshotHelper", m_screenshotHelper);
    }

private:
    StubAuthService *m_authService;
    StubShellController *m_shellController;
    StubSessionStore *m_sessionStore;
    FolderTreeTestHarness *m_folderTreeTestHarness;
    StubDriveManager *m_driveManager;
    StubProfileManager *m_profileManager;
    StubTransferManager *m_transferManager;
    StubShareManager *m_shareManager;
    StubTrashManager *m_trashManager;
    ScreenshotHelper *m_screenshotHelper;
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
