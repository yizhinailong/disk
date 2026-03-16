/**
 * @file FileService.hpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief QML 客户端高级文件编排服务
 *
 * @copyright Copyright (c) 2026
 *
 */
#pragma once

#include <QList>
#include <QObject>
#include <QString>
#include <functional>
#include <optional>

#include <dtos/FileDtos.hpp>

namespace disk::qml::api {
    class FileApi;
}

namespace disk::qml::services {
    class TokenRefreshCoordinator;

    /**
     * @brief QML 客户端文件服务。
     * @details 编排文件相关业务流程：
     *   - 分页和过滤列出文件/文件夹
     *   - 带输入验证的文件/文件夹重命名
     *   - 批量移动、复制和删除文件/文件夹
     *   - 按关键词和过滤条件搜索文件
     *   - 委托 api::FileApi 执行网络请求
     *   - 将传输层和响应封装层错误映射为用户友好消息
     */
    class FileService final {
    public:
        using ListCallback = std::function<void(std::optional<models::FileListResultDto> result, QString errorMessage)>;
        using RenameCallback = std::function<void(std::optional<models::RenameResultDto> result, QString errorMessage)>;
        using MoveCallback = std::function<void(std::optional<models::MoveResultDto> result, QString errorMessage)>;
        using CopyCallback = std::function<void(std::optional<models::CopyResultDto> result, QString errorMessage)>;
        using DeleteCallback = std::function<void(std::optional<models::DeleteResultDto> result, QString errorMessage)>;
        using SearchCallback = std::function<void(std::optional<models::SearchResultDto> result, QString errorMessage)>;

        explicit FileService(api::FileApi* fileApi, TokenRefreshCoordinator* coordinator = nullptr);

        /**
         * @brief 列出指定父文件夹中的文件和文件夹。
         * @param parentId   父文件夹 ID（0 = 根目录）。
         * @param page       页码（从 1 开始）。
         * @param pageSize   每页条数（1-100）。
         * @param sortBy     排序字段（name|size|created_at|updated_at）。
         * @param sortOrder  排序方向（asc|desc）。
         * @param type       过滤类型（all|file|folder）。
         * @param ctx        QObject 生命周期守护。
         * @param cb         接收 (result, errorMessage)。成功时 errorMessage 为空。
         */
        auto ListFiles(
            qint64 parentId,
            int page,
            int pageSize,
            const QString& sortBy,
            const QString& sortOrder,
            const QString& type,
            QObject* ctx,
            ListCallback cb
        ) -> void;

        /**
         * @brief 重命名文件或文件夹。
         * @details 调用 api::FileApi::Rename 前本地验证新名称。
         * @param fileId   文件/文件夹 ID（正整数）。
         * @param newName  新名称（不能为空或纯空白）。
         * @param ctx      QObject 生命周期守护。
         * @param cb       接收 (result, errorMessage)。成功时 errorMessage 为空。
         */
        auto RenameFile(qint64 fileId, const QString& newName, QObject* ctx, RenameCallback cb) -> void;

        /**
         * @brief 移动文件/文件夹到目标文件夹。
         * @details 调用 api::FileApi::Move 前验证 fileIds 非空。
         * @param fileIds         要移动的文件/文件夹 ID 列表。
         * @param targetFolderId  目标文件夹 ID（0 = 根目录）。
         * @param ctx             QObject 生命周期守护。
         * @param cb              接收 (result, errorMessage)。成功时 errorMessage 为空。
         */
        auto MoveFiles(const QList<qint64>& fileIds, qint64 targetFolderId, QObject* ctx, MoveCallback cb) -> void;

        /**
         * @brief 复制文件/文件夹到目标文件夹。
         * @details 调用 api::FileApi::Copy 前验证 fileIds 非空。
         * @param fileIds         要复制的文件/文件夹 ID 列表。
         * @param targetFolderId  目标文件夹 ID（0 = 根目录）。
         * @param ctx             QObject 生命周期守护。
         * @param cb              接收 (result, errorMessage)。成功时 errorMessage 为空。
         */
        auto CopyFiles(const QList<qint64>& fileIds, qint64 targetFolderId, QObject* ctx, CopyCallback cb) -> void;

        /**
         * @brief 软删除文件/文件夹（移至回收站）。
         * @details 调用 api::FileApi::Delete 前验证 fileIds 非空。
         * @param fileIds  要删除的文件/文件夹 ID 列表。
         * @param ctx      QObject 生命周期守护。
         * @param cb       接收 (result, errorMessage)。成功时 errorMessage 为空。
         */
        auto DeleteFiles(const QList<qint64>& fileIds, QObject* ctx, DeleteCallback cb) -> void;

        /**
         * @brief 按关键词搜索文件。
         * @details 调用 api::FileApi::Search 前本地验证关键词。
         * @param keyword   搜索关键词（必填，不能为空）。
         * @param type      过滤类型（all|file|folder）。
         * @param folderId  搜索范围（-1 = 全局搜索）。
         * @param page      页码（从 1 开始）。
         * @param pageSize  每页条数（1-100）。
         * @param ctx       QObject 生命周期守护。
         * @param cb        接收 (result, errorMessage)。成功时 errorMessage 为空。
         */
        auto SearchFiles(
            const QString& keyword,
            const QString& type,
            qint64 folderId,
            int page,
            int pageSize,
            QObject* ctx,
            SearchCallback cb
        ) -> void;

        /**
         * @brief 获取最近文件（按 updated_at 降序）。
         * @details 调用 ListFiles，sort_by=updated_at，sort_order=desc。
         * @param limit    返回的最大文件数（1-100，默认 10）。
         * @param ctx      QObject 生命周期守护。
         * @param cb       接收 (result, errorMessage)。成功时 errorMessage 为空。
         */
        auto GetRecentFiles(
            int limit,
            QObject* ctx,
            ListCallback cb
        ) -> void;

    private:
        auto MapTransportError(const QString& networkError) const -> QString;
        auto MapEnvelopeError(const models::ApiEnvelope& envelope) const -> QString;

        api::FileApi* m_file_api;
        TokenRefreshCoordinator* m_coordinator{ nullptr };
    };

} // namespace disk::qml::services
