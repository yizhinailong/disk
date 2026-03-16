/**
 * @file FolderService.hpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief High-level folder orchestration for the QML client
 *
 * @copyright Copyright (c) 2026
 *
 */
#pragma once

#include <QObject>
#include <QString>
#include <functional>
#include <optional>

#include <dtos/FileDtos.hpp>

namespace disk::qml::api {
    class FolderApi;
}

namespace disk::qml::services {
    class TokenRefreshCoordinator;

    /**
     * @brief QML 客户端文件夹服务。
     * @details 编排文件夹相关业务流程：
     *   - 带输入验证的文件夹创建
     *   - 获取导航面包屑路径
     *   - 获取移动/复制选择器的文件夹树结构
     *   - 委托 api::FolderApi 执行网络请求
     *   - 将传输层和响应封装层错误映射为用户友好消息
     */
    class FolderService final {
    public:
        using CreateFolderCallback = std::function<void(std::optional<models::CreateFolderResultDto> result, QString errorMessage)>;
        using BreadcrumbCallback = std::function<void(std::optional<models::BreadcrumbResultDto> result, QString errorMessage)>;
        using FolderTreeCallback = std::function<void(std::optional<models::FolderTreeResultDto> result, QString errorMessage)>;

        explicit FolderService(api::FolderApi* folderApi, TokenRefreshCoordinator* coordinator = nullptr);

        /**
         * @brief 创建新文件夹。
         * @details 调用 api::FolderApi::CreateFolder 前本地验证文件夹名称。
         *   成功时回调接收填充好的 CreateFolderResultDto。
         * @param name      文件夹名称（不能为空或仅含空白）。
         * @param parentId  父文件夹 ID（0 = 根目录）。
         * @param ctx       QObject 生命周期守护；ctx 销毁后不调用回调。
         * @param cb        接收 (result, errorMessage)。成功时 errorMessage 为空。
         */
        auto CreateFolder(const QString& name, qint64 parentId, QObject* ctx, CreateFolderCallback cb) -> void;

        /**
         * @brief 获取从根目录到指定文件夹的面包屑路径。
         * @param folderId  要获取面包屑的文件夹 ID。
         * @param ctx       QObject 生命周期守护。
         * @param cb        接收 (result, errorMessage)。成功时 errorMessage 为空。
         */
        auto GetBreadcrumb(qint64 folderId, QObject* ctx, BreadcrumbCallback cb) -> void;

        /**
         * @brief 获取移动/复制目标选择器的文件夹树结构。
         * @param parentId  子树根节点（0 = 根目录）。
         * @param depth     最大深度（-1 = 无限制）。
         * @param ctx       QObject 生命周期守护。
         * @param cb        接收 (result, errorMessage)。成功时 errorMessage 为空。
         */
        auto GetFolderTree(qint64 parentId, int depth, QObject* ctx, FolderTreeCallback cb) -> void;

    private:
        auto MapTransportError(const QString& networkError) const -> QString;
        auto MapEnvelopeError(const models::ApiEnvelope& envelope) const -> QString;

        api::FolderApi* m_folder_api;
        TokenRefreshCoordinator* m_coordinator{ nullptr };
    };

} // namespace disk::qml::services
