/**
 * @file TrashService.cpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief TrashService implementation
 *
 * @copyright Copyright (c) 2026
 *
 */
#include "TrashService.hpp"

#include <api/TrashApi.hpp>
#include <services/TokenRefreshCoordinator.hpp>
#include <utils/ErrorCode.hpp>

namespace disk::qml::services {

    TrashService::TrashService(api::TrashApi* trashApi, TokenRefreshCoordinator* coordinator)
        : m_trash_api(trashApi)
        , m_coordinator(coordinator) {
    }

    auto TrashService::ListTrash(
        int page,
        int pageSize,
        QObject* ctx,
        ListCallback cb
    ) -> void {
        m_trash_api->List(
            page,
            pageSize,
            ctx,
            [this, page, pageSize, ctx, cb, retried = false](models::ApiEnvelope envelope, QString networkError) mutable {
                if (!networkError.isEmpty()) {
                    cb(std::nullopt, MapTransportError(networkError));
                    return;
                }

                // Check for 40108 TokenExpired - retry once if coordinator available
                if (envelope.code == static_cast<int>(utils::ErrorCode::TokenExpired)) {
                    if (m_coordinator && !retried) {
                        retried = true;
                        m_coordinator->HandleIfTokenExpired(envelope,
                            [this, page, pageSize, ctx, cb, envelope](bool success) {
                                if (success) {
                                    // Retry the request
                                    m_trash_api->List(page, pageSize, ctx,
                                        [this, cb](models::ApiEnvelope retryEnvelope, QString retryNetworkError) {
                                            if (!retryNetworkError.isEmpty()) {
                                                cb(std::nullopt, MapTransportError(retryNetworkError));
                                                return;
                                            }
                                            if (retryEnvelope.code != static_cast<int>(utils::ErrorCode::Success)) {
                                                cb(std::nullopt, MapEnvelopeError(retryEnvelope));
                                                return;
                                            }
                                            auto parsed = models::ParseTrashListResult(retryEnvelope.data);
                                            if (!parsed) {
                                                cb(std::nullopt, QStringLiteral("服务器响应解析失败"));
                                                return;
                                            }
                                            cb(std::move(parsed), QString{});
                                        }
                                    );
                                } else {
                                    cb(std::nullopt, MapEnvelopeError(envelope));
                                }
                            }
                        );
                        return;
                    }
                }

                if (envelope.code != static_cast<int>(utils::ErrorCode::Success)) {
                    cb(std::nullopt, MapEnvelopeError(envelope));
                    return;
                }

                auto parsed = models::ParseTrashListResult(envelope.data);
                if (!parsed) {
                    cb(std::nullopt, QStringLiteral("服务器响应解析失败"));
                    return;
                }

                cb(std::move(parsed), QString{});
            }
        );
    }

    auto TrashService::RestoreItems(
        const QList<qint64>& trashIds,
        QObject* ctx,
        RestoreCallback cb
    ) -> void {
        if (trashIds.isEmpty()) {
            cb(
                std::nullopt,
                utils::ToUserMessage(static_cast<int>(utils::ErrorCode::InvalidParameter), QString{})
            );
            return;
        }

        m_trash_api->Restore(
            trashIds,
            ctx,
            [this, cb = std::move(cb)](models::ApiEnvelope envelope, QString networkError) {
                if (!networkError.isEmpty()) {
                    cb(std::nullopt, MapTransportError(networkError));
                    return;
                }

                if (envelope.code != static_cast<int>(utils::ErrorCode::Success)) {
                    cb(std::nullopt, MapEnvelopeError(envelope));
                    return;
                }

                auto parsed = models::ParseTrashBatchResult(envelope.data);
                if (!parsed) {
                    cb(std::nullopt, QStringLiteral("服务器响应解析失败"));
                    return;
                }

                cb(std::move(parsed), QString{});
            }
        );
    }

    auto TrashService::DeleteItems(
        const QList<qint64>& trashIds,
        QObject* ctx,
        DeleteCallback cb
    ) -> void {
        if (trashIds.isEmpty()) {
            cb(
                std::nullopt,
                utils::ToUserMessage(static_cast<int>(utils::ErrorCode::InvalidParameter), QString{})
            );
            return;
        }

        m_trash_api->Delete(
            trashIds,
            ctx,
            [this, cb = std::move(cb)](models::ApiEnvelope envelope, QString networkError) {
                if (!networkError.isEmpty()) {
                    cb(std::nullopt, MapTransportError(networkError));
                    return;
                }

                if (envelope.code != static_cast<int>(utils::ErrorCode::Success)) {
                    cb(std::nullopt, MapEnvelopeError(envelope));
                    return;
                }

                auto parsed = models::ParseTrashBatchResult(envelope.data);
                if (!parsed) {
                    cb(std::nullopt, QStringLiteral("服务器响应解析失败"));
                    return;
                }

                cb(std::move(parsed), QString{});
            }
        );
    }

    auto TrashService::ClearAll(QObject* ctx, ClearAllCallback cb) -> void {
        m_trash_api->ClearAll(
            ctx,
            [this, cb = std::move(cb)](models::ApiEnvelope envelope, QString networkError) {
                if (!networkError.isEmpty()) {
                    cb(std::nullopt, MapTransportError(networkError));
                    return;
                }

                if (envelope.code != static_cast<int>(utils::ErrorCode::Success)) {
                    cb(std::nullopt, MapEnvelopeError(envelope));
                    return;
                }

                auto parsed = models::ParseTrashClearResult(envelope.data);
                if (!parsed) {
                    cb(std::nullopt, QStringLiteral("服务器响应解析失败"));
                    return;
                }

                cb(std::move(parsed), QString{});
            }
        );
    }

    auto TrashService::MapTransportError(const QString& networkError) const -> QString {
        if (!networkError.isEmpty()) {
            return networkError;
        }
        return QStringLiteral("网络连接失败，请检查网络");
    }

    auto TrashService::MapEnvelopeError(const models::ApiEnvelope& envelope) const -> QString {
        return utils::ToUserMessage(envelope.code, envelope.message);
    }

} // namespace disk::qml::services
