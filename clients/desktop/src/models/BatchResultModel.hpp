/**
 * @file BatchResultModel.hpp
 * @brief QAbstractListModel for batch operation results per doc 02 §3.11
 *
 * @copyright Copyright (c) 2026
 */

#pragma once

#include <QAbstractListModel>
#include <QVector>

#include "network/ErrorAdapter.hpp"

namespace disk::desktop {

    struct BatchActionResultItem {
        QString resource_key;
        QString status;                            // "success" / "failed"
        std::optional<quint64> restored_item_id;
        std::optional<QString> restored_item_kind; // "file" / "folder"
        std::optional<QString> resolved_path;
        std::optional<quint64> freed_space;
        std::optional<ApiError> error;

        static auto FromJson(const QJsonObject& json, const QString& operation) -> BatchActionResultItem;
        auto ToJson() const -> QJsonObject;
    };

    struct BatchActionResult {
        QString operation; // "share_cancel" / "trash_restore" / "trash_delete"
        int total_count{ 0 };
        int success_count{ 0 };
        int failure_count{ 0 };
        QVector<BatchActionResultItem> items;

        static auto FromJson(const QJsonObject& json, const QString& operation) -> BatchActionResult;
        auto ToJson() const -> QJsonObject;
    };

    class BatchResultModel : public QAbstractListModel {
        Q_OBJECT

        Q_PROPERTY(QString operation READ GetOperation NOTIFY operationChanged)
        Q_PROPERTY(int totalCount READ GetTotalCount NOTIFY totalCountChanged)
        Q_PROPERTY(int successCount READ GetSuccessCount NOTIFY successCountChanged)
        Q_PROPERTY(int failureCount READ GetFailureCount NOTIFY failureCountChanged)

    public:
        enum Roles {
            ResourceKeyRole = Qt::UserRole + 1,
            StatusRole,
            RestoredItemIdRole,
            RestoredItemKindRole,
            ResolvedPathRole,
            FreedSpaceRole,
            ErrorRole,
        };

        explicit BatchResultModel(QObject* parent = nullptr);

        auto rowCount(const QModelIndex& parent = {}) const -> int override;
        auto data(const QModelIndex& index, int role) const -> QVariant override;
        auto roleNames() const -> QHash<int, QByteArray> override;

        auto SetResult(const BatchActionResult& result) -> void;
        auto Clear() -> void;

        auto GetOperation() const -> QString;
        auto GetTotalCount() const -> int;
        auto GetSuccessCount() const -> int;
        auto GetFailureCount() const -> int;

    signals:
        void operationChanged();
        void totalCountChanged();
        void successCountChanged();
        void failureCountChanged();

    private:
        BatchActionResult m_result;
    };

} // namespace disk::desktop
