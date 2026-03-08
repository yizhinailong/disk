/**
 * @file FolderService.cpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief FolderService implementation
 * @version 0.1
 * @date 2026-03-05
 *
 * @copyright Copyright (c) 2026
 *
 */
#include "FolderService.hpp"

#include <api/FolderApi.hpp>
#include <services/TokenRefreshCoordinator.hpp>
#include <utils/ErrorCode.hpp>

namespace disk::qml::services {

    FolderService::FolderService(api::FolderApi* folderApi, TokenRefreshCoordinator* coordinator)
        : m_folder_api(folderApi)
        , m_coordinator(coordinator) {
    }

    auto FolderService::CreateFolder(
        const QString& name,
        qint64 parentId,
        QObject* ctx,
        CreateFolderCallback cb
    ) -> void {
        if (name.trimmed().isEmpty()) {
            cb(
                std::nullopt,
                utils::ToUserMessage(static_cast<int>(utils::ErrorCode::InvalidParameter), QString{})
            );
            return;
        }

        m_folder_api->CreateFolder(
            name,
            parentId,
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

                auto parsed = models::ParseCreateFolderResult(envelope.data);
                if (!parsed) {
                    cb(std::nullopt, QStringLiteral("服务器响应解析失败"));
                    return;
                }

                cb(std::move(parsed), QString{});
            }
        );
    }

    auto FolderService::GetBreadcrumb(qint64 folderId, QObject* ctx, BreadcrumbCallback cb) -> void {
        m_folder_api->GetBreadcrumb(
            folderId,
            ctx,
            [this, folderId, ctx, cb, retried = false](models::ApiEnvelope envelope, QString networkError) mutable {
                if (!networkError.isEmpty()) {
                    cb(std::nullopt, MapTransportError(networkError));
                    return;
                }

                // Check for 40108 TokenExpired - retry once if coordinator available
                if (envelope.code == static_cast<int>(utils::ErrorCode::TokenExpired)) {
                    if (m_coordinator && !retried) {
                        retried = true;
                        m_coordinator->HandleIfTokenExpired(envelope,
                            [this, folderId, ctx, cb, envelope](bool success) {
                                if (success) {
                                    // Retry the request
                                    m_folder_api->GetBreadcrumb(folderId, ctx,
                                        [this, cb](models::ApiEnvelope retryEnvelope, QString retryNetworkError) {
                                            if (!retryNetworkError.isEmpty()) {
                                                cb(std::nullopt, MapTransportError(retryNetworkError));
                                                return;
                                            }
                                            if (retryEnvelope.code != static_cast<int>(utils::ErrorCode::Success)) {
                                                cb(std::nullopt, MapEnvelopeError(retryEnvelope));
                                                return;
                                            }
                                            auto parsed = models::ParseBreadcrumbResult(retryEnvelope.data);
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

                auto parsed = models::ParseBreadcrumbResult(envelope.data);
                if (!parsed) {
                    cb(std::nullopt, QStringLiteral("服务器响应解析失败"));
                    return;
                }

                cb(std::move(parsed), QString{});
            }
        );
    }

    auto FolderService::GetFolderTree(
        qint64 parentId,
        int depth,
        QObject* ctx,
        FolderTreeCallback cb
    ) -> void {
        m_folder_api->GetTree(
            parentId,
            depth,
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

                auto parsed = models::ParseFolderTreeResult(envelope.data);
                if (!parsed) {
                    cb(std::nullopt, QStringLiteral("服务器响应解析失败"));
                    return;
                }

                cb(std::move(parsed), QString{});
            }
        );
    }

    auto FolderService::MapTransportError(const QString& networkError) const -> QString {
        if (!networkError.isEmpty()) {
            return networkError;
        }
        return QStringLiteral("网络连接失败，请检查网络");
    }

    auto FolderService::MapEnvelopeError(const models::ApiEnvelope& envelope) const -> QString {
        return utils::ToUserMessage(envelope.code, envelope.message);
    }

} // namespace disk::qml::services
