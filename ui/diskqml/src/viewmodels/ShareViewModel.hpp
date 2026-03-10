/**
 * @file ShareViewModel.hpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief QML ViewModel for share management: list, create, cancel
 *
 * @copyright Copyright (c) 2026
 *
 * @details
 * Owns the share list state: loading/error, pagination, selection set.
 * Uses ShareService for all API interactions.
 * QML layer binds to properties and invokes Q_INVOKABLE methods.
 */

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
     * @brief QML ViewModel that drives the share page.
     *
     * @details
     * Coordinates listing, create, and cancel operations.
     * Delegates all network I/O to ShareService.
     *
     * Singleton boundary audit (Task 7): Page-scoped (share management page).
     * Kept as QML_SINGLETON for now to preserve typed registration and imports;
     * planned migration target is explicit page-level instantiation/injection.
     */
    class ShareViewModel : public QObject {
        Q_OBJECT
        QML_ELEMENT
        QML_SINGLETON

        // ==================== Loading / Error ====================

        /// True while an API call is in flight.
        Q_PROPERTY(bool loading READ Loading NOTIFY loadingChanged)
        /// Last error message; empty on success.
        Q_PROPERTY(QString errorMessage READ ErrorMessage NOTIFY errorMessageChanged)

        // ==================== Pagination ====================

        /// Current page number (1-based).
        Q_PROPERTY(int currentPage READ CurrentPage NOTIFY currentPageChanged)
        /// Total number of pages from the last response.
        Q_PROPERTY(int totalPages READ TotalPages NOTIFY totalPagesChanged)
        /// Total item count from the last response.
        Q_PROPERTY(int totalItems READ TotalItems NOTIFY totalItemsChanged)

        // ==================== Selection ====================

        /// Number of currently selected items.
        Q_PROPERTY(int selectionCount READ SelectionCount NOTIFY selectionChanged)
        /// True when at least one item is selected.
        Q_PROPERTY(bool hasSelection READ HasSelection NOTIFY selectionChanged)

        // ==================== Model ====================

        /// The share list model for QML binding.
        Q_PROPERTY(disk::qml::models::ShareListModel* shareListModel READ ShareListModelPtr CONSTANT)

    public:
        /**
         * @brief Password action for share update operations.
         *
         * @details
         * Explicitly defines the intent for password handling:
         * - Keep: Don't change the existing password
         * - Clear: Remove the password protection
         * - Set: Set a new password (value provided separately)
         */
        enum class PasswordAction {
            Keep,  ///< Keep existing password unchanged
            Clear, ///< Remove password protection
            Set,   ///< Set new password (use the provided password value)
        };
        Q_ENUM(PasswordAction)

    public:
        explicit ShareViewModel(
            services::ShareService* shareService,
            TransfersViewModel* transfersViewModel,
            QObject* parent = nullptr
        );

        // ==================== Singleton ====================

        /**
         * @brief Register the pre-created instance for use by the QML engine.
         */
        static auto SetInstance(ShareViewModel* instance) -> void;

        /**
         * @brief QML singleton factory — called once by the QML engine.
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

        // ==================== Actions ====================

        /// Reload the current page of share items.
        Q_INVOKABLE void refresh();

        /// Download a shared file
        Q_INVOKABLE void downloadFile(
            const QString& shareId,
            qint64 fileId,
            const QString& shareToken,
            const QString& destPath
        );

        /// Create a new share.
        Q_INVOKABLE void createShare(
            const QList<qint64>& fileIds,
            int expireDays,
            const QString& password,
            const QString& permission
        );

        /// Update an existing share's settings.
        /// @param shareId      Share ID to update.
        /// @param expireDays   New expiration in days (-1 = no change, 0 = permanent).
        /// @param passwordAction How to handle password (Keep/Clear/Set).
        /// @param password     New password value (only used when passwordAction == Set).
        /// @param permission   New permission ("view"/"download", empty = no change).
        Q_INVOKABLE void updateShare(
            const QString& shareId,
            int expireDays,
            PasswordAction passwordAction,
            const QString& password,
            const QString& permission
        );

        /// Cancel the currently selected shares.
        Q_INVOKABLE void cancelSelected();

        // ==================== Selection ====================

        /// Toggle selection of a single item by share ID.
        Q_INVOKABLE void toggleSelection(const QString& shareId);
        /// Select all items in the current list.
        Q_INVOKABLE void selectAll();
        /// Deselect all items.
        Q_INVOKABLE void clearSelection();
        /// Check if a specific item is selected.
        Q_INVOKABLE bool isSelected(const QString& shareId) const;
        /// Get list of all selected share IDs.
        Q_INVOKABLE QStringList selectedIds() const;

        // ==================== Pagination ====================

        /// Navigate to a specific page.
        Q_INVOKABLE void goToPage(int page);
        /// Parse comma-separated file IDs string into a list of integers.
        /// Returns empty list if parsing fails or input is invalid.
        /// Sets parseError property with error message if parsing fails.
        Q_INVOKABLE QList<qint64> parseFileIds(const QString& fileIdsText);
        
        /// Last parsing error message (empty on success).
        Q_PROPERTY(QString parseError READ ParseError NOTIFY parseErrorChanged)
        
        // Parse error getter
        [[nodiscard]] auto ParseError() const -> const QString&;

    signals:
        void loadingChanged();
        void errorMessageChanged();
        void currentPageChanged();
        void totalPagesChanged();
        void totalItemsChanged();
        void selectionChanged();
        /// Emitted when a share operation (cancel) succeeds.
        void shareOperationSucceeded(const QString& message);
        /// Emitted when a share operation fails.
        void shareOperationFailed(const QString& message);

        /// Emitted when a share download starts.
        void downloadStarted(qint64 fileId, const QString& fileName);
        /// Emitted when a share download fails to start.
        void downloadFailed(const QString& error);
        /// Emitted when a share is created successfully.
        void shareCreated(
            const QString& shareId,
            const QString& shareLink,
            const QString& password,
            const QString& expiresAt
        );
        /// Emitted when a share is updated successfully.
        void shareUpdated(
            const QString& shareId,
            const QString& expiresAt,
            bool hasPassword,
            const QString& permission
        );
        /// Emitted when share update fails.
        void updateFailed(const QString& message);
        /// Emitted when parse error changes.
        void parseErrorChanged();

    private:
        // ==================== Private Helpers ====================

        auto SetLoading(bool loading) -> void;
        auto SetErrorMessage(const QString& message) -> void;
        auto SetParseError(const QString& error) -> void;

        /// Fetch the share list for the current page.
        auto FetchShareList() -> void;

        // ==================== State ====================

        services::ShareService* m_share_service;
        TransfersViewModel* m_transfers_view_model;

        models::ShareListModel* m_share_list_model;

        bool m_loading{ false };
        QString m_error_message;
        QString m_parse_error;

        // Pagination
        int m_current_page{ 1 };
        int m_total_pages{ 0 };
        int m_total_items{ 0 };
        static constexpr int kPageSize = 100;

        // Selection (share IDs are strings)
        QSet<QString> m_selected_ids;

        inline static ShareViewModel* s_instance = nullptr;
        inline static QJSEngine* s_engine = nullptr;
    };

} // namespace disk::qml::viewmodels
