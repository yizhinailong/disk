/**
 * @file FolderApi.hpp
 * @brief 文件夹 API 客户端
 * @details 提供文件夹创建、目录树、面包屑导航等文件夹相关的 HTTP API 调用
 * @author LiuFeng (liufeng.code@outlook.com)
 * @version 0.1
 * @date 2026-03-05
 *
 * @copyright Copyright (c) 2026
 */
#pragma once

#include <QObject>
#include <QString>

#include <dtos/ApiEnvelope.hpp>

namespace disk::qml::api {

    using FolderApiCallback = models::ApiCallback;

    class ApiClient;

    /**
     * @brief 文件夹相关 API 封装（均需 JWT 认证）
     *
     * @details
     * 所有方法使用 ApiClient 上配置的共享 Bearer 令牌。
     * 调用者需确保在调用这些方法前已设置令牌（通过 ApiClient::SetBearerToken）。
     *
     * 封装的端点：
     * - POST /api/folder/create                   -> CreateFolderResponse
     * - GET  /api/folder/tree                      -> FolderTreeNode[]
     * - GET  /api/folder/{folder_id}/breadcrumb    -> BreadcrumbResponse
     */
    class FolderApi {
    public:
        /**
         * @brief 构造文件夹 API 客户端
         *
         * @param client API 客户端指针，调用者需确保该指针的生命周期长于此实例
         */
        explicit FolderApi(ApiClient* client);

        /**
         * @brief POST /api/folder/create - 创建新文件夹
         *
         * @details
         * 请求体: { "name": "<string>", "parent_id": <uint64> }
         * 响应数据结构: { id, name, parent_id, path, created_at }
         *
         * @param name 文件夹名称（1-255 个可打印 ASCII 字符）
         * @param parentId 父文件夹 ID（0 = 根目录）
         * @param ctx 上下文 QObject，销毁时取消回调
         * @param cb 服务器响应回调
         */
        virtual auto CreateFolder(
            const QString& name,
            qint64 parentId,
            QObject* ctx,
            FolderApiCallback cb
        ) -> void;

        /**
         * @brief GET /api/folder/tree - 获取文件夹树结构
         *
         * @details
         * 查询参数：
         * - parent_id  (默认 0，根目录)
         * - depth      (默认 -1，无限深度)
         *
         * 响应数据结构: 递归 { id, name, children: [...] }
         *
         * @param parentId 子树根节点（0 = 根目录）
         * @param depth 最大深度（-1 = 无限制）
         * @param ctx 上下文 QObject，销毁时取消回调
         * @param cb 服务器响应回调
         */
        virtual auto GetTree(
            qint64 parentId,
            int depth,
            QObject* ctx,
            FolderApiCallback cb
        ) -> void;

        /**
         * @brief GET /api/folder/{folder_id}/breadcrumb - 获取面包屑路径
         *
         * @details
         * 响应数据结构: { "path": [ { id, name }, ... ] }
         *
         * @param folderId 文件夹 ID
         * @param ctx 上下文 QObject，销毁时取消回调
         * @param cb 服务器响应回调
         */
        virtual auto GetBreadcrumb(qint64 folderId, QObject* ctx, FolderApiCallback cb) -> void;

        virtual ~FolderApi() = default;

    private:
        ApiClient* m_client;
    };

} // namespace disk::qml::api
