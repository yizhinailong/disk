/**
 * @file FileService.cpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief FileService 实现
 *
 * @copyright Copyright (c) 2026
 *
 */
#include "FileService.hpp"

#include <api/FileApi.hpp>
#include <services/TokenRefreshCoordinator.hpp>
#include <utils/ErrorCode.hpp>

namespace disk::qml::services {

    FileService::FileService(api::FileApi* fileApi, TokenRefreshCoordinator* coordinator)
        : m_file_api(fileApi), m_coordinator(coordinator) {
    }

    auto FileService::ListFiles(
        qint64 parentId,
        int page,
        int pageSize,
        const QString& sortBy,
        const QString& sortOrder,
        const QString& type,
        QObject* ctx,
        ListCallback cb
    ) -> void {
        auto performRequest = [this, parentId, page, pageSize, sortBy, sortOrder, type, ctx, cb, retried = false]() mutable {
            m_file_api->List(
                parentId,
                page,
                pageSize,
                sortBy,
                sortOrder,
                type,
                ctx,
                [this, parentId, page, pageSize, sortBy, sortOrder, type, ctx, cb, retried](models::ApiEnvelope envelope, QString networkError) mutable {
                    if (!networkError.isEmpty()) {
                        cb(std::nullopt, MapTransportError(networkError));
                        return;
                    }

                    // 检查 40108 TokenExpired - 如果协调器可用则重试一次
                    if (envelope.code == static_cast<int>(utils::ErrorCode::TokenExpired)) {
                        if (m_coordinator && !retried) {
                            retried = true;
                            m_coordinator->HandleIfTokenExpired(envelope, [this, parentId, page, pageSize, sortBy, sortOrder, type, ctx, cb, retried, envelope](bool success) {
                                if (success) {
                                    // 使用原始参数重试请求
                                    m_file_api->List(parentId, page, pageSize, sortBy, sortOrder, type, ctx, [this, cb](models::ApiEnvelope retryEnvelope, QString retryNetworkError) {
                                        if (!retryNetworkError.isEmpty()) {
                                            cb(std::nullopt, MapTransportError(retryNetworkError));
                                            return;
                                        }
                                        if (retryEnvelope.code != static_cast<int>(utils::ErrorCode::Success)) {
                                            cb(std::nullopt, MapEnvelopeError(retryEnvelope));
                                            return;
                                        }
                                        auto parsed = models::ParseFileListResult(retryEnvelope.data);
                                        if (!parsed) {
                                            cb(std::nullopt, QStringLiteral("服务器响应解析失败"));
                                            return;
                                        }
                                        cb(std::move(parsed), QString{});
                                    });
                                } else {
                                    // 刷新失败，返回错误
                                    cb(std::nullopt, MapEnvelopeError(envelope));
                                }
                            });
                            return;
                        }
                    }

                    // 检查 40108 TokenExpired - 如果协调器是否可用，则重试该请求

                    auto parsed = models::ParseFileListResult(envelope.data);
                    if (!parsed) {
                        cb(std::nullopt, QStringLiteral("服务器响应解析失败"));
                        return;
                    }

                    cb(std::move(parsed), QString{});
                }
            );
        };
        performRequest();
    }

    auto FileService::RenameFile(
        qint64 fileId,
        const QString& newName,
        QObject* ctx,
        RenameCallback cb
    ) -> void {
        if (newName.trimmed().isEmpty()) {
            cb(
                std::nullopt,
                utils::ToUserMessage(static_cast<int>(utils::ErrorCode::InvalidParameter), QString{})
            );
            return;
        }

        m_file_api->Rename(
            fileId,
            newName,
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

                auto parsed = models::ParseRenameResult(envelope.data);
                if (!parsed) {
                    cb(std::nullopt, QStringLiteral("服务器响应解析失败"));
                    return;
                }

                cb(std::move(parsed), QString{});
            }
        );
    }

    auto FileService::MoveFiles(
        const QList<qint64>& fileIds,
        qint64 targetFolderId,
        QObject* ctx,
        MoveCallback cb
    ) -> void {
        if (fileIds.isEmpty()) {
            cb(
                std::nullopt,
                utils::ToUserMessage(static_cast<int>(utils::ErrorCode::InvalidParameter), QString{})
            );
            return;
        }

        m_file_api->Move(
            fileIds,
            targetFolderId,
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

                auto parsed = models::ParseMoveResult(envelope.data);
                if (!parsed) {
                    cb(std::nullopt, QStringLiteral("服务器响应解析失败"));
                    return;
                }

                cb(std::move(parsed), QString{});
            }
        );
    }

    auto FileService::CopyFiles(
        const QList<qint64>& fileIds,
        qint64 targetFolderId,
        QObject* ctx,
        CopyCallback cb
    ) -> void {
        if (fileIds.isEmpty()) {
            cb(
                std::nullopt,
                utils::ToUserMessage(static_cast<int>(utils::ErrorCode::InvalidParameter), QString{})
            );
            return;
        }

        m_file_api->Copy(
            fileIds,
            targetFolderId,
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

                auto parsed = models::ParseCopyResult(envelope.data);
                if (!parsed) {
                    cb(std::nullopt, QStringLiteral("服务器响应解析失败"));
                    return;
                }

                cb(std::move(parsed), QString{});
            }
        );
    }

    auto FileService::DeleteFiles(
        const QList<qint64>& fileIds,
        QObject* ctx,
        DeleteCallback cb
    ) -> void {
        if (fileIds.isEmpty()) {
            cb(
                std::nullopt,
                utils::ToUserMessage(static_cast<int>(utils::ErrorCode::InvalidParameter), QString{})
            );
            return;
        }

        m_file_api->Delete(
            fileIds,
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

                auto parsed = models::ParseDeleteResult(envelope.data);
                if (!parsed) {
                    cb(std::nullopt, QStringLiteral("服务器响应解析失败"));
                    return;
                }

                cb(std::move(parsed), QString{});
            }
        );
    }

    auto FileService::SearchFiles(
        const QString& keyword,
        const QString& type,
        qint64 folderId,
        int page,
        int pageSize,
        QObject* ctx,
        SearchCallback cb
    ) -> void {
        if (keyword.trimmed().isEmpty()) {
            cb(
                std::nullopt,
                utils::ToUserMessage(static_cast<int>(utils::ErrorCode::InvalidParameter), QString{})
            );
            return;
        }

        m_file_api->Search(
            keyword,
            type,
            folderId,
            page,
            pageSize,
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

                auto parsed = models::ParseSearchResult(envelope.data);
                if (!parsed) {
                    cb(std::nullopt, QStringLiteral("服务器响应解析失败"));
                    return;
                }

                cb(std::move(parsed), QString{});
            }
        );
    }

    auto FileService::GetRecentFiles(
        int limit,
        QObject* ctx,
        ListCallback cb
    ) -> void {
        // 将限制限制在有效范围 [1, 100]
        const int clampedLimit = qBound(1, limit, 100);

        // 使用根文件夹 (0) 调用 ListFiles，按 updated_at 降序排序
        ListFiles(
            0,                            // parent_id（根目录）
            1,                            // page
            clampedLimit,                 // page_size
            QStringLiteral("updated_at"), // sort_by
            QStringLiteral("desc"),       // sort_order
            QStringLiteral("all"),        // type
            ctx,
            cb
        );
    }

    auto FileService::MapTransportError(const QString& networkError) const -> QString {
        if (!networkError.isEmpty()) {
            return networkError;
        }
        return QStringLiteral("网络连接失败，请检查网络");
    }

    auto FileService::MapEnvelopeError(const models::ApiEnvelope& envelope) const -> QString {
        return utils::ToUserMessage(envelope.code, envelope.message);
    }

} // namespace disk::qml::services
