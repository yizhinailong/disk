/**
 * @file FileListViewModel.hpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 文件列表视图模型，管理导航、排序、搜索和选择
 *
 * @copyright Copyright (c) 2026
 *
 * @details
 * 管理文件列表状态：当前文件夹、面包屑路径、加载/错误状态、
 * 视图模式、排序控制、搜索（防抖）、导航历史（前进/后退）、
 * 多选选择集。
 *
 * 使用 FileService 和 FolderService 处理所有 API 交互。
 * QML 层绑定属性并调用 Q_INVOKABLE 方法。
 */

#pragma once

#include <QObject>
#include <QSet>
#include <QString>
#include <QTimer>
#include <QVector>

#include <QtQml/qjsengine.h>
#include <QtQml/qqmlengine.h>
#include <QtQml/qqmlregistration.h>

namespace disk::qml::models {
    class FileListModel;
    class BreadcrumbModel;
    class FolderTreeModel;
} // namespace disk::qml::models

namespace disk::qml::services {
    class FileService;
    class FolderService;
} // namespace disk::qml::services

namespace disk::qml::viewmodels {

    /**
     * @brief 驱动主文件浏览器视图的 QML 视图模型。
     *
     * @details
     * 协调导航、排序、搜索和选择状态。
     * 将所有网络 I/O 委托给 FileService 和 FolderService。
     *
     * 导航历史是一对简单的栈（后退/前进），跟踪访问过的文件夹 ID，
     * 实现浏览器式的前进/后退导航。
     *
     * 搜索使用基于 QTimer 的防抖（300ms），避免快速输入时
     * 向服务器发送过多请求。
     *
     * 单例边界审计（任务 7）：页面作用域（文件浏览器页面状态）。
     * 暂时保留 QML_SINGLETON 以保持类型注册和当前 QML 导入；
     * 计划迁移目标为显式实例化/注入。
     */
    class FileListViewModel : public QObject {
        Q_OBJECT
        QML_ELEMENT
        QML_SINGLETON

        // ==================== 导航状态 ====================

        Q_PROPERTY(qint64 currentFolderId READ CurrentFolderId NOTIFY currentFolderIdChanged) ///< 当前显示的文件夹 ID（0 = 根目录）
        Q_PROPERTY(QString currentPath READ CurrentPath NOTIFY currentPathChanged)            ///< 面包屑组装的可读路径（如 "/ 文档 / 工作"）
        // ==================== 加载/错误 ====================

        Q_PROPERTY(bool loading READ Loading NOTIFY loadingChanged)                   ///< 列表或搜索 API 请求进行中时为 true
        Q_PROPERTY(QString errorMessage READ ErrorMessage NOTIFY errorMessageChanged) ///< 最后的错误消息；成功时为空
        // ==================== 视图模式 ====================

        Q_PROPERTY(QString viewMode READ ViewMode WRITE SetViewMode NOTIFY viewModeChanged) ///< 显示模式："grid" 或 "list"
        // ==================== 排序 ====================

        Q_PROPERTY(QString sortBy READ SortBy WRITE SetSortBy NOTIFY sortByChanged)             ///< 排序字段："name"、"size"、"created_at"、"updated_at"
        Q_PROPERTY(QString sortOrder READ SortOrder WRITE SetSortOrder NOTIFY sortOrderChanged) ///< 排序方向："asc" 或 "desc"
        // ==================== 搜索 ====================

        Q_PROPERTY(QString searchKeyword READ SearchKeyword WRITE SetSearchKeyword NOTIFY searchKeywordChanged) ///< 当前搜索关键词；未搜索时为空
        Q_PROPERTY(bool isSearching READ IsSearching NOTIFY isSearchingChanged)                                 ///< 搜索模式激活时为 true（非空关键词且有结果）
        // ==================== 分页 ====================

        Q_PROPERTY(int currentPage READ CurrentPage NOTIFY currentPageChanged) ///< 当前页码（从 1 开始）
        Q_PROPERTY(int totalPages READ TotalPages NOTIFY totalPagesChanged)    ///< 上次响应的总页数
        Q_PROPERTY(int totalItems READ TotalItems NOTIFY totalItemsChanged)    ///< 上次响应的总条目数
        // ==================== 选择 ====================

        Q_PROPERTY(int selectionCount READ SelectionCount NOTIFY selectionChanged) ///< 当前选中的条目数
        Q_PROPERTY(bool hasSelection READ HasSelection NOTIFY selectionChanged)    ///< 至少选中一个条目时为 true
        // ==================== 模型 ====================

        Q_PROPERTY(disk::qml::models::FileListModel* fileListModel READ FileListModelPtr CONSTANT)       ///< QML 绑定用的文件列表模型
        Q_PROPERTY(disk::qml::models::BreadcrumbModel* breadcrumbModel READ BreadcrumbModelPtr CONSTANT) ///< QML 绑定用的面包屑模型
        Q_PROPERTY(disk::qml::models::FolderTreeModel* folderTreeModel READ FolderTreeModelPtr CONSTANT) ///< QML 文件夹选择对话框绑定的文件夹树模型
        // ==================== 最近文件 ====================

        Q_PROPERTY(disk::qml::models::FileListModel* recentFilesModel READ RecentFilesModelPtr CONSTANT) ///< QML 绑定用的最近文件模型（最多 10 条，按更新时间倒序）
        Q_PROPERTY(bool recentFilesLoading READ RecentFilesLoading NOTIFY recentFilesLoadingChanged)     ///< 正在加载最近文件时为 true
        Q_PROPERTY(QString recentFilesError READ RecentFilesError NOTIFY recentFilesErrorChanged)        ///< 最近文件加载失败时的错误消息
        // ==================== 导航历史 ====================

        Q_PROPERTY(bool canGoBack READ CanGoBack NOTIFY navigationHistoryChanged)       ///< 可以后退导航时为 true
        Q_PROPERTY(bool canGoForward READ CanGoForward NOTIFY navigationHistoryChanged) ///< 可以前进导航时为 true

    public:
        explicit FileListViewModel(
            services::FileService* fileService,
            services::FolderService* folderService,
            QObject* parent = nullptr
        );

        // ==================== Singleton ====================

        /**
         * @brief Register the pre-created instance for use by the QML engine.
         */
        static auto SetInstance(FileListViewModel* instance) -> void;

        /**
         * @brief QML singleton factory — called once by the QML engine.
         */
        static auto create(QQmlEngine* qmlEngine, QJSEngine* jsEngine) -> FileListViewModel*;

        // ==================== Property Getters ====================

        [[nodiscard]] auto CurrentFolderId() const -> qint64;
        [[nodiscard]] auto CurrentPath() const -> const QString&;
        [[nodiscard]] auto Loading() const -> bool;
        [[nodiscard]] auto ErrorMessage() const -> const QString&;
        [[nodiscard]] auto ViewMode() const -> const QString&;
        [[nodiscard]] auto SortBy() const -> const QString&;
        [[nodiscard]] auto SortOrder() const -> const QString&;
        [[nodiscard]] auto SearchKeyword() const -> const QString&;
        [[nodiscard]] auto IsSearching() const -> bool;
        [[nodiscard]] auto CurrentPage() const -> int;
        [[nodiscard]] auto TotalPages() const -> int;
        [[nodiscard]] auto TotalItems() const -> int;
        [[nodiscard]] auto SelectionCount() const -> int;
        [[nodiscard]] auto HasSelection() const -> bool;
        [[nodiscard]] auto CanGoBack() const -> bool;
        [[nodiscard]] auto CanGoForward() const -> bool;
        [[nodiscard]] auto FileListModelPtr() const -> models::FileListModel*;
        [[nodiscard]] auto BreadcrumbModelPtr() const -> models::BreadcrumbModel*;
        [[nodiscard]] auto FolderTreeModelPtr() const -> models::FolderTreeModel*;
        [[nodiscard]] auto RecentFilesModelPtr() const -> models::FileListModel*;
        [[nodiscard]] auto RecentFilesLoading() const -> bool;
        [[nodiscard]] auto RecentFilesError() const -> const QString&;

        // ==================== Property Setters ====================

        auto SetViewMode(const QString& mode) -> void;
        auto SetSortBy(const QString& field) -> void;
        auto SetSortOrder(const QString& order) -> void;
        auto SetSearchKeyword(const QString& keyword) -> void;

        // ==================== 导航 ====================

        Q_INVOKABLE void navigateToFolder(qint64 folderId); ///< 进入指定 ID 的子文件夹
        Q_INVOKABLE void navigateUp();                      ///< 进入父文件夹（上一级）
        Q_INVOKABLE void goBack();                          ///< 后退导航
        Q_INVOKABLE void goForward();                       ///< 前进导航
        Q_INVOKABLE void refresh();                         ///< 刷新当前文件夹内容
        // ==================== 搜索 ====================

        Q_INVOKABLE void search(const QString& keyword); ///< 使用指定关键词触发搜索（防抖）
        Q_INVOKABLE void clearSearch();                  ///< 清除搜索并返回普通文件夹视图
        // ==================== 排序 ====================

        Q_INVOKABLE void setSortField(const QString& field); ///< 设置排序字段并重新加载
        Q_INVOKABLE void toggleSortOrder();                  ///< 切换排序方向（升序 ↔ 降序）并重新加载
        // ==================== 选择 ====================

        Q_INVOKABLE void toggleSelection(qint64 fileId);  ///< 切换单个条目的选中状态
        Q_INVOKABLE void selectAll();                     ///< 选中当前列表中的所有条目
        Q_INVOKABLE void clearSelection();                ///< 取消所有选中
        Q_INVOKABLE bool isSelected(qint64 fileId) const; ///< 检查指定条目是否被选中
        Q_INVOKABLE QList<qint64> selectedIds() const;    ///< 获取所有选中的文件 ID 列表
        // ==================== 分页 ====================

        Q_INVOKABLE void goToPage(int page); ///< 跳转到指定页
        // ==================== 文件操作 ====================

        Q_INVOKABLE void createFolder(const QString& name);                              ///< 在当前目录创建新文件夹
        Q_INVOKABLE void renameFile(qint64 fileId, const QString& newName);              ///< 重命名文件或文件夹
        Q_INVOKABLE void deleteFiles(const QList<qint64>& fileIds);                      ///< 软删除文件/文件夹（移入回收站）。如果 fileIds 为空，使用当前选择
        Q_INVOKABLE void moveFiles(const QList<qint64>& fileIds, qint64 targetFolderId); ///< 将文件/文件夹移动到目标文件夹
        Q_INVOKABLE void copyFiles(const QList<qint64>& fileIds, qint64 targetFolderId); ///< 将文件/文件夹复制到目标文件夹
        Q_INVOKABLE void loadFolderTree();                                               ///< 加载文件夹树（用于文件夹选择对话框）
        // ==================== 最近文件 ====================

        Q_INVOKABLE void loadRecentFiles(); ///< 加载最近文件（最多 10 条，按更新时间倒序）
    signals:
        void currentFolderIdChanged();
        void currentPathChanged();
        void loadingChanged();
        void errorMessageChanged();
        void viewModeChanged();
        void sortByChanged();
        void sortOrderChanged();
        void searchKeywordChanged();
        void isSearchingChanged();
        void currentPageChanged();
        void totalPagesChanged();
        void totalItemsChanged();
        void selectionChanged();
        void navigationHistoryChanged();
        /// 文件操作（创建/重命名/删除）成功时发射
        void fileOperationSucceeded(const QString& message);
        /// 文件操作（创建/重命名/删除）失败时发射
        void fileOperationFailed(const QString& message);

        // ==================== 最近文件 ====================
        void recentFilesLoadingChanged();
        void recentFilesErrorChanged();

    private:
        // ==================== 私有辅助方法 ====================

        auto SetLoading(bool loading) -> void;
        auto SetErrorMessage(const QString& message) -> void;
        auto SetCurrentFolderId(qint64 id) -> void;
        auto SetCurrentPath(const QString& path) -> void;

        auto FetchFileList() -> void;                    ///< 获取当前文件夹、页码和排序设置的文件列表
        auto FetchBreadcrumb() -> void;                  ///< 获取当前文件夹的面包屑路径
        auto ExecuteSearch() -> void;                    ///< 执行防抖搜索查询

        auto BuildPathFromBreadcrumb() -> QString;       ///< 从面包屑模型构建可读路径字符串
        auto PushToBackHistory(qint64 folderId) -> void; ///< 将当前文件夹压入后退历史（导航前调用）
        // ==================== 状态 ====================

        services::FileService* m_file_service;         ///< 文件服务
        services::FolderService* m_folder_service;     ///< 文件夹服务

        models::FileListModel* m_file_list_model;      ///< 文件列表模型
        models::BreadcrumbModel* m_breadcrumb_model;   ///< 面包屑模型
        models::FolderTreeModel* m_folder_tree_model;  ///< 文件夹树模型

        qint64 m_current_folder_id{ 0 };               ///< 当前文件夹 ID
        QString m_current_path;                        ///< 当前路径
        bool m_loading{ false };                       ///< 是否正在加载
        QString m_error_message;                       ///< 错误消息
        QString m_view_mode{ QStringLiteral("list") }; ///< 视图模式
        QString m_sort_by{ QStringLiteral("name") };   ///< 排序字段
        QString m_sort_order{ QStringLiteral("asc") }; ///< 排序方向

        // 搜索
        QString m_search_keyword;       ///< 搜索关键词
        bool m_is_searching{ false };   ///< 是否正在搜索
        QTimer m_search_debounce_timer; ///< 搜索防抖定时器

        // 分页
        int m_current_page{ 1 };             ///< 当前页码
        int m_total_pages{ 0 };              ///< 总页数
        int m_total_items{ 0 };              ///< 总条目数
        static constexpr int kPageSize = 50; ///< 每页条目数
        // 选择
        QSet<qint64> m_selected_ids; ///< 选中的文件 ID 集合

        // 导航历史
        QVector<qint64> m_back_history;    ///< 后退历史栈
        QVector<qint64> m_forward_history; ///< 前进历史栈

        // 最近文件
        models::FileListModel* m_recent_files_model;           ///< 最近文件模型
        bool m_recent_files_loading{ false };                  ///< 是否正在加载最近文件
        QString m_recent_files_error;                          ///< 最近文件错误消息
        static constexpr int kRecentFilesLimit = 10;           ///< 最近文件数量上限

        inline static FileListViewModel* s_instance = nullptr; ///< 单例实例
        inline static QJSEngine* s_engine = nullptr;           ///< JS 引擎实例
    };

} // namespace disk::qml::viewmodels
