/**
 * @file TrashViewModel.cpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief TrashViewModel implementation
 * @version 0.1
 * @date 2026-03-05
 *
 * @copyright Copyright (c) 2026
 *
 */
#include "TrashViewModel.hpp"

#include <models/TrashListModel.hpp>
#include <services/TrashService.hpp>

namespace disk::qml::viewmodels {

    // ==================== Constructor ====================

    TrashViewModel::TrashViewModel(
        services::TrashService* trashService,
        QObject* parent
    ) : QObject(parent),
        m_trash_service(trashService),
        m_trash_list_model(new models::TrashListModel(this)) {
    }

    // ==================== Singleton ====================

    auto TrashViewModel::SetInstance(TrashViewModel* instance) -> void {
        s_instance = instance;
    }

    auto TrashViewModel::create(QQmlEngine* qmlEngine, QJSEngine* jsEngine) -> TrashViewModel* {
        Q_ASSERT(s_instance);
        Q_ASSERT(!s_engine || s_engine == jsEngine);
        s_engine = jsEngine;

        // C++ side owns the instance; prevent engine from deleting it.
        QJSEngine::setObjectOwnership(s_instance, QJSEngine::CppOwnership);
        return s_instance;
    }

    // ==================== Property Getters ====================

    auto TrashViewModel::Loading() const -> bool {
        return m_loading;
    }

    auto TrashViewModel::ErrorMessage() const -> const QString& {
        return m_error_message;
    }

    auto TrashViewModel::CurrentPage() const -> int {
        return m_current_page;
    }

    auto TrashViewModel::TotalPages() const -> int {
        return m_total_pages;
    }

    auto TrashViewModel::TotalItems() const -> int {
        return m_total_items;
    }

    auto TrashViewModel::SelectionCount() const -> int {
        return static_cast<int>(m_selected_ids.size());
    }

    auto TrashViewModel::HasSelection() const -> bool {
        return !m_selected_ids.isEmpty();
    }

    auto TrashViewModel::TrashListModelPtr() const -> models::TrashListModel* {
        return m_trash_list_model;
    }

    // ==================== Private Helpers ====================

    auto TrashViewModel::SetLoading(bool loading) -> void {
        if (m_loading != loading) {
            m_loading = loading;
            emit loadingChanged();
        }
    }

    auto TrashViewModel::SetErrorMessage(const QString& message) -> void {
        if (m_error_message != message) {
            m_error_message = message;
            emit errorMessageChanged();
        }
    }

    // ==================== Actions ====================

    void TrashViewModel::refresh() {
        FetchTrashList();
    }

    void TrashViewModel::restoreSelected() {
        if (m_selected_ids.isEmpty()) {
            return;
        }

        SetLoading(true);

        QList<qint64> ids(m_selected_ids.begin(), m_selected_ids.end());
        auto* ctx = new QObject(this);

        m_trash_service->RestoreItems(
            ids,
            ctx,
            [this, ctx](std::optional<models::TrashBatchResultDto> result, QString errorMessage) {
                ctx->deleteLater();
                SetLoading(false);

                if (!errorMessage.isEmpty()) {
                    emit trashOperationFailed(errorMessage);
                    return;
                }

                if (result) {
                    clearSelection();
                    const auto& summary = result->summary;
                    emit trashOperationSucceeded(
                        QStringLiteral("已恢复 %1 项").arg(summary.successCount)
                    );
                    FetchTrashList();
                }
            }
        );
    }

    void TrashViewModel::deleteSelected() {
        if (m_selected_ids.isEmpty()) {
            return;
        }

        SetLoading(true);

        QList<qint64> ids(m_selected_ids.begin(), m_selected_ids.end());
        auto* ctx = new QObject(this);

        m_trash_service->DeleteItems(
            ids,
            ctx,
            [this, ctx](std::optional<models::TrashBatchResultDto> result, QString errorMessage) {
                ctx->deleteLater();
                SetLoading(false);

                if (!errorMessage.isEmpty()) {
                    emit trashOperationFailed(errorMessage);
                    return;
                }

                if (result) {
                    clearSelection();
                    const auto& summary = result->summary;
                    emit trashOperationSucceeded(
                        QStringLiteral("已彻底删除 %1 项").arg(summary.successCount)
                    );
                    FetchTrashList();
                }
            }
        );
    }

    void TrashViewModel::clearAll() {
        SetLoading(true);

        auto* ctx = new QObject(this);

        m_trash_service->ClearAll(
            ctx,
            [this, ctx](std::optional<models::TrashClearResultDto> result, QString errorMessage) {
                ctx->deleteLater();
                SetLoading(false);

                if (!errorMessage.isEmpty()) {
                    emit trashOperationFailed(errorMessage);
                    return;
                }

                if (result) {
                    clearSelection();
                    emit trashOperationSucceeded(
                        QStringLiteral("已清空回收站，删除 %1 项").arg(result->deletedCount)
                    );
                    FetchTrashList();
                }
            }
        );
    }

    // ==================== Selection ====================

    void TrashViewModel::toggleSelection(qint64 trashId) {
        if (m_selected_ids.contains(trashId)) {
            m_selected_ids.remove(trashId);
        } else {
            m_selected_ids.insert(trashId);
        }
        emit selectionChanged();
    }

    void TrashViewModel::selectAll() {
        for (int i = 0; i < m_trash_list_model->Count(); ++i) {
            auto item = m_trash_list_model->ItemAt(i);
            if (item) {
                m_selected_ids.insert(static_cast<qint64>(item->id));
            }
        }
        emit selectionChanged();
    }

    void TrashViewModel::clearSelection() {
        if (m_selected_ids.isEmpty()) {
            return;
        }
        m_selected_ids.clear();
        emit selectionChanged();
    }

    bool TrashViewModel::isSelected(qint64 trashId) const {
        return m_selected_ids.contains(trashId);
    }

    QList<qint64> TrashViewModel::selectedIds() const {
        return QList<qint64>(m_selected_ids.begin(), m_selected_ids.end());
    }

    // ==================== Pagination ====================

    void TrashViewModel::goToPage(int page) {
        if (page < 1 || page == m_current_page) {
            return;
        }
        m_current_page = page;
        emit currentPageChanged();
        FetchTrashList();
    }

    // ==================== Private: Fetch ====================

    auto TrashViewModel::FetchTrashList() -> void {
        SetLoading(true);
        SetErrorMessage(QString{});

        auto* ctx = new QObject(this);

        m_trash_service->ListTrash(
            m_current_page,
            kPageSize,
            ctx,
            [this, ctx](std::optional<models::TrashListResultDto> result, QString errorMessage) {
                ctx->deleteLater();
                SetLoading(false);

                if (!errorMessage.isEmpty()) {
                    SetErrorMessage(errorMessage);
                    m_trash_list_model->Clear();
                    return;
                }

                if (!result) {
                    SetErrorMessage(QStringLiteral("服务器响应解析失败"));
                    m_trash_list_model->Clear();
                    return;
                }

                // Map DTOs to model data
                QVector<models::TrashListItemData> items;
                items.reserve(result->items.size());
                for (const auto& dto : result->items) {
                    models::TrashListItemData data;
                    data.id = dto.id;
                    data.type = dto.type;
                    data.originalId = dto.originalId;
                    data.name = dto.name;
                    data.size = static_cast<qint64>(dto.size);
                    data.originalPath = dto.originalPath;
                    data.deletedAt = dto.deletedAt;
                    data.expiresAt = dto.expiresAt;
                    items.append(data);
                }

                m_trash_list_model->ResetItems(items);

                // Update pagination
                const auto& pag = result->pagination;
                if (m_current_page != pag.page) {
                    m_current_page = pag.page;
                    emit currentPageChanged();
                }
                if (m_total_pages != pag.totalPages) {
                    m_total_pages = pag.totalPages;
                    emit totalPagesChanged();
                }
                if (m_total_items != pag.total) {
                    m_total_items = pag.total;
                    emit totalItemsChanged();
                }
            }
        );
    }

} // namespace disk::qml::viewmodels
