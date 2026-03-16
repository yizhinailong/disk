/**
 * @file TrashViewModel.hpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 回收站视图模型，管理回收站列表、恢复、删除和清空
 *
 * @copyright Copyright (c) 2026
 *
 * @details
 * 管理回收站列表状态：加载/错误、分页、选择集。
 * 使用 TrashService 处理所有 API 交互。
 * QML 层绑定属性并调用 Q_INVOKABLE 方法。
 */

#pragma once

#include <QObject>
#include <QSet>
#include <QString>

#include <QtQml/qjsengine.h>
#include <QtQml/qqmlengine.h>
#include <QtQml/qqmlregistration.h>

namespace disk::qml::models {
    class TrashListModel;
} // namespace disk::qml::models

namespace disk::qml::services {
    class TrashService;
} // namespace disk::qml::services

namespace disk::qml::viewmodels {

    /**
     * @brief 驱动回收站页面的 QML 视图模型。
     *
     * @details
     * 协调列表、恢复、永久删除和清空操作。
     * 将所有网络 I/O 委托给 TrashService。
     *
     * 单例边界审计（任务 7）：页面作用域（回收站页面状态）。
     * 暂时保留 QML_SINGLETON 以保持类型注册和导入；
     * 计划迁移目标为显式页面级实例化/注入。
     */
    class TrashViewModel : public QObject {
        Q_OBJECT
        QML_ELEMENT
        QML_SINGLETON

        // ==================== 加载/错误 ====================

        Q_PROPERTY(bool loading READ Loading NOTIFY loadingChanged)                   ///< API 请求进行中时为 true
        Q_PROPERTY(QString errorMessage READ ErrorMessage NOTIFY errorMessageChanged) ///< 最后的错误消息；成功时为空

        // ==================== 分页 ====================

        Q_PROPERTY(int currentPage READ CurrentPage NOTIFY currentPageChanged) ///< 当前页码（从 1 开始）
        Q_PROPERTY(int totalPages READ TotalPages NOTIFY totalPagesChanged)    ///< 上次响应的总页数
        Q_PROPERTY(int totalItems READ TotalItems NOTIFY totalItemsChanged)    ///< 上次响应的总条目数

        // ==================== 选择 ====================

        Q_PROPERTY(int selectionCount READ SelectionCount NOTIFY selectionChanged) ///< 当前选中的条目数
        Q_PROPERTY(bool hasSelection READ HasSelection NOTIFY selectionChanged)    ///< 至少选中一个条目时为 true

        // ==================== 模型 ====================

        Q_PROPERTY(disk::qml::models::TrashListModel* trashListModel READ TrashListModelPtr CONSTANT) ///< QML 绑定用的回收站列表模型

    public:
        explicit TrashViewModel(
            services::TrashService* trashService,
            QObject* parent = nullptr
        );

        // ==================== Singleton ====================

        /**
         * @brief 注册预创建的实例供 QML 引擎使用。
         */
        static auto SetInstance(TrashViewModel* instance) -> void;

        /**
         * @brief QML 单例工厂——由 QML 引擎调用一次。
         */
        static auto create(QQmlEngine* qmlEngine, QJSEngine* jsEngine) -> TrashViewModel*;

        // ==================== Property Getters ====================

        [[nodiscard]] auto Loading() const -> bool;
        [[nodiscard]] auto ErrorMessage() const -> const QString&;
        [[nodiscard]] auto CurrentPage() const -> int;
        [[nodiscard]] auto TotalPages() const -> int;
        [[nodiscard]] auto TotalItems() const -> int;
        [[nodiscard]] auto SelectionCount() const -> int;
        [[nodiscard]] auto HasSelection() const -> bool;
        [[nodiscard]] auto TrashListModelPtr() const -> models::TrashListModel*;

        // ==================== 操作 ====================

        Q_INVOKABLE void refresh();         ///< 刷新当前页的回收站条目

        Q_INVOKABLE void restoreSelected(); ///< 恢复当前选中的条目

        Q_INVOKABLE void deleteSelected();  ///< 永久删除当前选中的条目

        Q_INVOKABLE void clearAll();        ///< 清空回收站

        // ==================== 选择 ====================

        Q_INVOKABLE void toggleSelection(qint64 trashId);  ///< 切换单个条目的选中状态
        Q_INVOKABLE void selectAll();                      ///< 选中当前列表中的所有条目
        Q_INVOKABLE void clearSelection();                 ///< 取消所有选中
        Q_INVOKABLE bool isSelected(qint64 trashId) const; ///< 检查指定条目是否被选中
        Q_INVOKABLE QList<qint64> selectedIds() const;     ///< 获取所有选中的回收站 ID 列表

        // ==================== 分页 ====================

        Q_INVOKABLE void goToPage(int page);                             ///< 跳转到指定页
        Q_INVOKABLE bool isExpiringSoon(const QString& expiresAt) const; ///< 检查条目是否即将过期（7 天内）
        Q_INVOKABLE int daysUntilExpiry(const QString& expiresAt) const; ///< 计算距离过期的天数（无效/空则返回 -1）

    signals:
        void loadingChanged();
        void errorMessageChanged();
        void currentPageChanged();
        void totalPagesChanged();
        void totalItemsChanged();
        void selectionChanged();
        /// 回收站操作（恢复/删除/清空）成功时发射
        void trashOperationSucceeded(const QString& message);
        /// 回收站操作（恢复/删除/清空）失败时发射
        void trashOperationFailed(const QString& message);

    private:
        // ==================== 私有辅助方法 ====================

        auto SetLoading(bool loading) -> void;
        auto SetErrorMessage(const QString& message) -> void;

        auto FetchTrashList() -> void; ///< 获取当前页的回收站列表

        // ==================== 状态 ====================

        services::TrashService* m_trash_service;    ///< 回收站服务

        models::TrashListModel* m_trash_list_model; ///< 回收站列表模型

        bool m_loading{ false };                    ///< 是否正在加载
        QString m_error_message;                    ///< 错误消息

        // 分页
        int m_current_page{ 1 };              ///< 当前页码
        int m_total_pages{ 0 };               ///< 总页数
        int m_total_items{ 0 };               ///< 总条目数
        static constexpr int kPageSize = 100; ///< 每页条目数

        // 选择
        QSet<qint64> m_selected_ids;                        ///< 选中的回收站 ID 集合

        inline static TrashViewModel* s_instance = nullptr; ///< 单例实例
        inline static QJSEngine* s_engine = nullptr;        ///< JS 引擎实例
    };

} // namespace disk::qml::viewmodels
