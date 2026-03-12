/**
 * @file ShareViewModel.hpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 分享视图模型，管理分享列表、创建和取消
 *
 * @copyright Copyright (c) 2026
 *
 * @details
 * 管理分享列表状态：加载/错误、分页、选择集。
 * 使用 ShareService 处理所有 API 交互。
 * QML 层绑定属性并调用 Q_INVOKABLE 方法。

#pragma once

#include <QObject>
#include <QSet>
#include <QString>

#include <QtQml/qjsengine.h>
#include <QtQml/qqmlengine.h>
#include <QtQml/qqmlregistration.h>

namespace disk::qml::models {
    class ShareListModel;
} // namespace disk::qml::models

namespace disk::qml::services {
    class ShareService;
} // namespace disk::qml::services

namespace disk::qml::viewmodels {

    class TransfersViewModel;

    /**
     * @brief 驱动分享页面的 QML 视图模型。
     *
     * @details
     * 协调列表、创建和取消操作。
     * 将所有网络 I/O 委托给 ShareService。
     *
     * 单例边界审计（任务 7）：页面作用域（分享管理页面）。
     * 暂时保留 QML_SINGLETON 以保持类型注册和导入；
     * 计划迁移目标为显式页面级实例化/注入。
     */
    class ShareViewModel : public QObject {
        Q_OBJECT
        QML_ELEMENT
        QML_SINGLETON

        // ==================== 加载/错误 ====================

        Q_PROPERTY(bool loading READ Loading NOTIFY loadingChanged) ///< API 请求进行中时为 true
        Q_PROPERTY(QString errorMessage READ ErrorMessage NOTIFY errorMessageChanged) ///< 最后的错误消息；成功时为空

        // ==================== 分页 ====================

        Q_PROPERTY(int currentPage READ CurrentPage NOTIFY currentPageChanged) ///< 当前页码（从 1 开始）
        Q_PROPERTY(int totalPages READ TotalPages NOTIFY totalPagesChanged) ///< 上次响应的总页数
        Q_PROPERTY(int totalItems READ TotalItems NOTIFY totalItemsChanged) ///< 上次响应的总条目数

        // ==================== 选择 ====================

        Q_PROPERTY(int selectionCount READ SelectionCount NOTIFY selectionChanged) ///< 当前选中的条目数
        Q_PROPERTY(bool hasSelection READ HasSelection NOTIFY selectionChanged) ///< 至少选中一个条目时为 true

        // ==================== 模型 ====================

        Q_PROPERTY(disk::qml::models::ShareListModel* shareListModel READ ShareListModelPtr CONSTANT) ///< QML 绑定用的分享列表模型
    public:
        /**
         * @brief 分享更新操作的密码处理方式。
         *
         * @details
         * 显式定义密码处理的意图：
         * - Keep：不更改现有密码
         * - Clear：移除密码保护
         * - Set：设置新密码（值单独提供）
         */
        enum class PasswordAction {
            Keep,  ///< 保持现有密码不变
            Clear, ///< 移除密码保护
            Set,   ///< 设置新密码（使用提供的密码值）
        };

    public:
        explicit ShareViewModel(
            services::ShareService* shareService,
            TransfersViewModel* transfersViewModel,
            QObject* parent = nullptr
        );

        // ==================== Singleton ====================

        /**
         * @brief 注册预创建的实例供 QML 引擎使用。
         */
        static auto SetInstance(ShareViewModel* instance) -> void;

        /**
         * @brief QML 单例工厂——由 QML 引擎调用一次。
         */
        static auto create(QQmlEngine* qmlEngine, QJSEngine* jsEngine) -> ShareViewModel*;

        // ==================== Property Getters ====================

        [[nodiscard]] auto Loading() const -> bool;
        [[nodiscard]] auto ErrorMessage() const -> const QString&;
        [[nodiscard]] auto CurrentPage() const -> int;
        [[nodiscard]] auto TotalPages() const -> int;
        [[nodiscard]] auto TotalItems() const -> int;
        [[nodiscard]] auto SelectionCount() const -> int;
        [[nodiscard]] auto HasSelection() const -> bool;
        [[nodiscard]] auto ShareListModelPtr() const -> models::ShareListModel*;

        // ==================== 操作 ====================

        Q_INVOKABLE void refresh(); ///< 刷新当前页的分享条目

        Q_INVOKABLE void downloadFile(
            const QString& shareId,
            qint64 fileId,
            const QString& shareToken,
            const QString& destPath
        ); ///< 下载分享文件

        Q_INVOKABLE void createShare(
            const QList<qint64>& fileIds,
            int expireDays,
            const QString& password,
            const QString& permission
        ); ///< 创建新分享

        /// 更新现有分享的设置。
        /// @param shareId      要更新的分享 ID。
        /// @param expireDays   新的过期天数（-1 = 不变，0 = 永久）。
        /// @param passwordAction 密码处理方式（Keep/Clear/Set）。
        /// @param password     新密码值（仅当 passwordAction == Set 时使用）。
        /// @param permission   新权限（"view"/"download"，空 = 不变）。
        Q_INVOKABLE void updateShare(
            const QString& shareId,
            int expireDays,
            PasswordAction passwordAction,
            const QString& password,
            const QString& permission
        );

        Q_INVOKABLE void cancelSelected(); ///< 取消当前选中的分享

        // ==================== 选择 ====================

        Q_INVOKABLE void toggleSelection(const QString& shareId); ///< 切换单个条目的选中状态
        Q_INVOKABLE void selectAll(); ///< 选中当前列表中的所有条目
        Q_INVOKABLE void clearSelection(); ///< 取消所有选中
        Q_INVOKABLE bool isSelected(const QString& shareId) const; ///< 检查指定条目是否被选中
        Q_INVOKABLE QStringList selectedIds() const; ///< 获取所有选中的分享 ID 列表

        // ==================== 分页 ====================

        Q_INVOKABLE void goToPage(int page); ///< 跳转到指定页
        /// 解析逗号分隔的文件 ID 字符串为整数列表。
        /// 如果解析失败或输入无效则返回空列表。
        /// 如果解析失败则设置 parseError 属性为错误消息。
        Q_INVOKABLE QList<qint64> parseFileIds(const QString& fileIdsText);
        
        Q_PROPERTY(QString parseError READ ParseError NOTIFY parseErrorChanged) ///< 最后的解析错误消息（成功时为空）
        
        // 解析错误获取器
        [[nodiscard]] auto ParseError() const -> const QString&;

    signals:
        void loadingChanged();
        void errorMessageChanged();
        void currentPageChanged();
        void totalPagesChanged();
        void totalItemsChanged();
        void selectionChanged();
        /// 分享操作（取消）成功时发射
        void shareOperationSucceeded(const QString& message);
        /// 分享操作失败时发射
        void shareOperationFailed(const QString& message);

        /// 分享下载开始时发射
        void downloadStarted(qint64 fileId, const QString& fileName);
        /// 分享下载启动失败时发射
        void downloadFailed(const QString& error);
        /// 分享创建成功时发射
        void shareCreated(
            const QString& shareId,
            const QString& shareLink,
            const QString& password,
            const QString& expiresAt
        );
        /// 分享更新成功时发射
        void shareUpdated(
            const QString& shareId,
            const QString& expiresAt,
            bool hasPassword,
            const QString& permission
        );
        /// 分享更新失败时发射
        void updateFailed(const QString& message);
        /// 解析错误变化时发射
        void parseErrorChanged();

        // ==================== 私有辅助方法 ====================

        auto SetLoading(bool loading) -> void;
        auto SetErrorMessage(const QString& message) -> void;
        auto SetParseError(const QString& error) -> void;

        auto FetchShareList() -> void; ///< 获取当前页的分享列表

        // ==================== 状态 ====================

        services::ShareService* m_share_service; ///< 分享服务
        TransfersViewModel* m_transfers_view_model; ///< 传输视图模型

        models::ShareListModel* m_share_list_model; ///< 分享列表模型

        bool m_loading{ false }; ///< 是否正在加载
        QString m_error_message; ///< 错误消息
        QString m_parse_error; ///< 解析错误

        // 分页
        int m_current_page{ 1 }; ///< 当前页码
        int m_total_pages{ 0 }; ///< 总页数
        int m_total_items{ 0 }; ///< 总条目数
        static constexpr int kPageSize = 100; ///< 每页条目数

        // 选择（分享 ID 为字符串）
        QSet<QString> m_selected_ids; ///< 选中的分享 ID 集合

        inline static ShareViewModel* s_instance = nullptr; ///< 单例实例
        inline static QJSEngine* s_engine = nullptr; ///< JS 引擎实例

} // namespace disk::qml::viewmodels
