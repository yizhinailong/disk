/**
 * @file FileApi.hpp
 * @brief 文件上传服务 API 客户端
 * @details 提供文件列表、详情、下载、重命名、移动、复制、删除、搜索等文件相关的 HTTP API 调用
 * @author LiuFeng (liufeng.code@outlook.com)
 *
 * @copyright Copyright (c) 2026
 */
#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

#include <api/ApiClient.hpp>
#include <dtos/ApiEnvelope.hpp>

namespace disk::qml::api {

    using FileApiCallback = models::ApiCallback;

    class ApiClient;

    /**
     * @brief 文件相关 API 封装（均需 JWT 认证）
     *
     * @details
     * 所有方法使用 ApiClient 上配置的共享 Bearer 令牌。
     * 调用者需确保在调用这些方法前已设置令牌（通过 ApiClient::SetBearerToken）。
     *
     * 封装的端点：
     * - GET    /api/file/list                      -> FileListResponse
     * - GET    /api/file/{file_id}                  -> FileDetailResponse
     * - GET    /api/file/download/{file_id}/info    -> DownloadInfoResponse
     * - GET    /api/file/download/{file_id}         -> binary (download engine)
     * - PUT    /api/file/{file_id}/rename           -> RenameResponse
     * - PUT    /api/file/move                       -> MoveResponse
     * - POST   /api/file/copy                       -> CopyResponse
     * - DELETE  /api/file                            -> DeleteResponse
     * - GET    /api/file/search                     -> SearchResponse
     */
    class FileApi {
    public:
        /**
         * @brief 构造文件 API 客户端
         *
         * @param client API 客户端指针，调用者需确保该指针的生命周期长于此实例
         */
        explicit FileApi(ApiClient* client);

        /**
         * @brief GET /api/file/list - 获取分页文件列表
         *
         * @details
         * 查询参数：
         * - parent_id   (默认 0，根文件夹)
         * - page        (默认 1)
         * - page_size   (默认 20，最大 100)
         * - sort_by     (name|size|created_at|updated_at，默认 name)
         * - sort_order  (asc|desc，默认 asc)
         * - type        (all|file|folder，默认 all)
         *
         * @param parentId 父文件夹 ID（0 = 根目录）
         * @param page 页码（从 1 开始）
         * @param pageSize 每页条数（1-100）
         * @param sortBy 排序字段
         * @param sortOrder 排序方向
         * @param type 过滤类型
         * @param ctx 上下文 QObject，销毁时取消回调
         * @param cb 服务器响应回调
         */
        virtual auto List(
            qint64 parentId,
            int page,
            int pageSize,
            const QString& sortBy,
            const QString& sortOrder,
            const QString& type,
            QObject* ctx,
            FileApiCallback cb
        ) -> void;

        /**
         * @brief GET /api/file/{file_id} - 获取文件详情
         *
         * @param fileId 文件 ID（正整数）
         * @param ctx 上下文 QObject，销毁时取消回调
         * @param cb 服务器响应回调
         */
        virtual auto GetDetail(qint64 fileId, QObject* ctx, FileApiCallback cb) -> void;

        /**
         * @brief GET /api/file/download/{file_id}/info - 获取下载元数据
         *
         * @details
         * 响应数据结构: { file_id, filename, file_size, file_hash,
         *                mime_type, supports_range }
         *
         * @param fileId 文件 ID（正整数）
         * @param ctx 上下文 QObject，销毁时取消回调
         * @param cb 服务器响应回调
         */
        virtual auto DownloadInfo(qint64 fileId, QObject* ctx, FileApiCallback cb) -> void;

        /**
         * @brief GET /api/file/download/{file_id} - 下载文件二进制数据
         *
         * @note 返回原始二进制数据，回调接收原始字节而非 JSON 封装，供下载引擎使用。
         *
         * @param fileId 文件 ID（正整数）
         * @param ctx 上下文 QObject，销毁时取消回调
         * @param cb 原始响应回调（状态码/字节）
         */
        virtual auto Download(qint64 fileId, QObject* ctx, ApiReplyCallback cb) -> void;

        /**
         * @brief PUT /api/file/{file_id}/rename - 重命名文件
         *
         * @param fileId 文件 ID（正整数）
         * @param newName 新文件名
         * @param ctx 上下文 QObject，销毁时取消回调
         * @param cb 服务器响应回调
         */
        virtual auto Rename(
            qint64 fileId,
            const QString& newName,
            QObject* ctx,
            FileApiCallback cb
        ) -> void;

        /**
         * @brief PUT /api/file/move - 移动文件到目标文件夹
         *
         * @param fileIds 要移动的文件 ID 列表
         * @param targetFolderId 目标文件夹 ID（0 = 根目录）
         * @param ctx 上下文 QObject，销毁时取消回调
         * @param cb 服务器响应回调
         */
        virtual auto Move(
            const QList<qint64>& fileIds,
            qint64 targetFolderId,
            QObject* ctx,
            FileApiCallback cb
        ) -> void;

        /**
         * @brief POST /api/file/copy - 复制文件到目标文件夹
         *
         * @param fileIds 要复制的文件 ID 列表
         * @param targetFolderId 目标文件夹 ID（0 = 根目录）
         * @param ctx 上下文 QObject，销毁时取消回调
         * @param cb 服务器响应回调
         */
        virtual auto Copy(
            const QList<qint64>& fileIds,
            qint64 targetFolderId,
            QObject* ctx,
            FileApiCallback cb
        ) -> void;

        /**
         * @brief DELETE /api/file - 软删除文件（移至回收站）
         *
         * @param fileIds 要删除的文件 ID 列表
         * @param ctx 上下文 QObject，销毁时取消回调
         * @param cb 服务器响应回调
         */
        virtual auto Delete(
            const QList<qint64>& fileIds,
            QObject* ctx,
            FileApiCallback cb
        ) -> void;

        /**
         * @brief GET /api/file/search - 按关键词搜索文件
         *
         * @details
         * 查询参数：
         * - keyword    (必填，1-100 字符)
         * - type       (all|file|folder，默认 all)
         * - folder_id  (可选，范围搜索)
         * - page       (默认 1)
         * - page_size  (默认 20，最大 100)
         *
         * @param keyword 搜索关键词（必填）
         * @param type 过滤类型
         * @param folderId 搜索范围（-1 = 全局搜索）
         * @param page 页码（从 1 开始）
         * @param pageSize 每页条数（1-100）
         * @param ctx 上下文 QObject，销毁时取消回调
         * @param cb 服务器响应回调
         */
        virtual auto Search(
            const QString& keyword,
            const QString& type,
            qint64 folderId,
            int page,
            int pageSize,
            QObject* ctx,
            FileApiCallback cb
        ) -> void;

        virtual ~FileApi() = default;

    private:
        ApiClient* m_client;
    };

} // namespace disk::qml::api
