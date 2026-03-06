/**
 * @file ShareViewModel.cpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief ShareViewModel implementation
 * @version 0.1
 * @date 2026-03-05
 *
 * @copyright Copyright (c) 2026
 *
 */
#include "ShareViewModel.hpp"

#include <models/ShareListModel.hpp>
#include <services/ShareService.hpp>

namespace disk::qml::viewmodels {

    // ==================== Constructor ====================

    ShareViewModel::ShareViewModel(
        services::ShareService* shareService,
        QObject* parent
    ) : QObject(parent),
        m_share_service(shareService),
        m_share_list_model(new models::ShareListModel(this)) {
    }

    // ==================== Singleton ====================

    auto ShareViewModel::SetInstance(ShareViewModel* instance) -> void {
        s_instance = instance;
    }

    auto ShareViewModel::create(QQmlEngine* qmlEngine, QJSEngine* jsEngine) -> ShareViewModel* {
        Q_ASSERT(s_instance);
        Q_ASSERT(!s_engine || s_engine == jsEngine);
        s_engine = jsEngine;

        // C++ side owns the instance; prevent engine from deleting it.
        QJSEngine::setObjectOwnership(s_instance, QJSEngine::CppOwnership);
        return s_instance;
    }

    // ==================== Property Getters ====================

    auto ShareViewModel::Loading() const -> bool {
        return m_loading;
    }

    auto ShareViewModel::ErrorMessage() const -> const QString& {
        return m_error_message;
    }

    auto ShareViewModel::CurrentPage() const -> int {
        return m_current_page;
    }

    auto ShareViewModel::TotalPages() const -> int {
        return m_total_pages;
    }

    auto ShareViewModel::TotalItems() const -> int {
        return m_total_items;
    }

    auto ShareViewModel::SelectionCount() const -> int {
        return static_cast<int>(m_selected_ids.size());
    }

    auto ShareViewModel::HasSelection() const -> bool {
        return !m_selected_ids.isEmpty();
    }

    auto ShareViewModel::ShareListModelPtr() const -> models::ShareListModel* {
        return m_share_list_model;
    }

    // ==================== Private Helpers ====================

    auto ShareViewModel::SetLoading(bool loading) -> void {
        if (m_loading != loading) {
            m_loading = loading;
            emit loadingChanged();
        }
    }

    auto ShareViewModel::SetErrorMessage(const QString& message) -> void {
        if (m_error_message != message) {
            m_error_message = message;
            emit errorMessageChanged();
        }
    }

    // ==================== Actions ====================

    void ShareViewModel::refresh() {
        FetchShareList();
    }

    void ShareViewModel::createShare(
        const QList<qint64>& fileIds,
        int expireDays,
        const QString& password,
        const QString& permission
    ) {
        SetLoading(true);

        auto* ctx = new QObject(this);

        m_share_service->CreateShare(
            fileIds,
            expireDays,
            password,
            permission,
            ctx,
            [this, ctx](std::optional<models::CreateShareResultDto> result, QString errorMessage) {
                ctx->deleteLater();
                SetLoading(false);

                if (!errorMessage.isEmpty()) {
                    emit shareOperationFailed(errorMessage);
                    return;
                }

                if (result) {
                    emit shareCreated(
                        result->shareId,
                        result->shareLink,
                        result->password.value_or(QString{}),
                        result->expiresAt
                    );
                    FetchShareList();
                }
            }
        );
    }

    void ShareViewModel::cancelSelected() {
        if (m_selected_ids.isEmpty()) {
            return;
        }

        SetLoading(true);

        QStringList ids(m_selected_ids.begin(), m_selected_ids.end());
        auto* ctx = new QObject(this);

        m_share_service->CancelShares(
            ids,
            ctx,
            [this, ctx](std::optional<models::CancelShareResultDto> result, QString errorMessage) {
                ctx->deleteLater();
                SetLoading(false);

                if (!errorMessage.isEmpty()) {
                    emit shareOperationFailed(errorMessage);
                    return;
                }

                if (result) {
                    clearSelection();
                    const auto& summary = result->summary;
                    emit shareOperationSucceeded(
                        QStringLiteral("已取消 %1 个分享").arg(summary.succeeded)
                    );
                    FetchShareList();
                }
            }
        );
    }

    // ==================== Selection ====================

    void ShareViewModel::toggleSelection(const QString& shareId) {
        if (m_selected_ids.contains(shareId)) {
            m_selected_ids.remove(shareId);
        } else {
            m_selected_ids.insert(shareId);
        }
        emit selectionChanged();
    }

    void ShareViewModel::selectAll() {
        for (int i = 0; i < m_share_list_model->Count(); ++i) {
            auto item = m_share_list_model->ItemAt(i);
            if (item) {
                m_selected_ids.insert(item->shareId);
            }
        }
        emit selectionChanged();
    }

    void ShareViewModel::clearSelection() {
        if (m_selected_ids.isEmpty()) {
            return;
        }
        m_selected_ids.clear();
        emit selectionChanged();
    }

    bool ShareViewModel::isSelected(const QString& shareId) const {
        return m_selected_ids.contains(shareId);
    }

    QStringList ShareViewModel::selectedIds() const {
        return QStringList(m_selected_ids.begin(), m_selected_ids.end());
    }

    // ==================== Pagination ====================

    void ShareViewModel::goToPage(int page) {
        if (page < 1 || page == m_current_page) {
            return;
        }
        m_current_page = page;
        emit currentPageChanged();
        FetchShareList();
    }

    // ==================== Private: Fetch ====================

    auto ShareViewModel::FetchShareList() -> void {
        SetLoading(true);
        SetErrorMessage(QString{});

        auto* ctx = new QObject(this);

        m_share_service->ListShares(
            QStringLiteral("all"),
            m_current_page,
            kPageSize,
            ctx,
            [this, ctx](std::optional<models::ShareListResultDto> result, QString errorMessage) {
                ctx->deleteLater();
                SetLoading(false);

                if (!errorMessage.isEmpty()) {
                    SetErrorMessage(errorMessage);
                    m_share_list_model->Clear();
                    return;
                }

                if (!result) {
                    SetErrorMessage(QStringLiteral("服务器响应解析失败"));
                    m_share_list_model->Clear();
                    return;
                }

                // Map DTOs to model data
                QVector<models::ShareListItemData> items;
                items.reserve(result->items.size());
                for (const auto& dto : result->items) {
                    models::ShareListItemData data;
                    data.shareId = dto.shareId;
                    data.fileName = dto.fileName;
                    data.fileCount = dto.fileCount;
                    data.shareLink = dto.shareLink;
                    data.hasPassword = dto.hasPassword;
                    data.permission = dto.permission;
                    data.viewCount = dto.viewCount;
                    data.downloadCount = dto.downloadCount;
                    data.createdAt = dto.createdAt;
                    data.expiresAt = dto.expiresAt;
                    data.status = dto.status;
                    items.append(data);
                }

                m_share_list_model->ResetItems(items);

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
