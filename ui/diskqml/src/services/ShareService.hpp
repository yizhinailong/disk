/**
 * @file ShareService.hpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief QML 客户端高级分享编排服务
 *
 * @copyright Copyright (c) 2026
 *
 */
#pragma once

#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <functional>
#include <optional>

#include <dtos/ShareDtos.hpp>

namespace disk::qml::api {
    class ShareApi;
}

namespace disk::qml::services {

    class TokenRefreshCoordinator;

    /**
     * @brief QML 客户端分享服务。
     * @details 编排分享相关业务流程：
     *   - 创建分享
     *   - 带状态过滤和分页的分享列表
     *   - 批量取消分享
     *   - 委托 api::ShareApi 执行网络请求
     *   - 将传输层和响应封装层错误映射为用户友好消息
     */
    class ShareService final {
    public:
        using CreateCallback = std::function<void(std::optional<models::CreateShareResultDto> result, QString errorMessage)>;
        using ListCallback = std::function<void(std::optional<models::ShareListResultDto> result, QString errorMessage)>;
        using CancelCallback = std::function<void(std::optional<models::CancelShareResultDto> result, QString errorMessage)>;
        using UpdateCallback = std::function<void(std::optional<models::UpdateShareResultDto> result, QString errorMessage)>;

        explicit ShareService(api::ShareApi* shareApi, TokenRefreshCoordinator* coordinator = nullptr);

        /**
         * @brief 创建新分享。
         * @param fileIds      要分享的文件 ID 列表。
         * @param expireDays   有效期天数（0 = 永久，默认 7 天）。
         * @param password     可选访问密码（4-8 个字符，空 = 无密码）。
         * @param permission   "view" 或 "download"。
         * @param ctx          QObject 生命周期守护。
         * @param cb           接收 (result, errorMessage)。成功时 errorMessage 为空。
         */
        auto CreateShare(
            const QList<qint64>& fileIds,
            int expireDays,
            const QString& password,
            const QString& permission,
            QObject* ctx,
            CreateCallback cb
        ) -> void;

        /**
         * @brief 带状态过滤和分页列出分享。
         * @param status    过滤条件："all"、"active"、"expired"、"cancelled"。
         * @param page      页码（从 1 开始）。
         * @param pageSize  每页条数（1-100）。
         * @param ctx       QObject 生命周期守护。
         * @param cb        接收 (result, errorMessage)。成功时 errorMessage 为空。
         */
        auto ListShares(
            const QString& status,
            int page,
            int pageSize,
            QObject* ctx,
            ListCallback cb
        ) -> void;

        /**
         * @brief 批量取消分享。
         * @details 调用 api::ShareApi::Cancel 前验证 shareIds 非空。
         * @param shareIds  要取消的分享 ID 字符串列表。
         * @param ctx       QObject 生命周期守护。
         * @param cb        接收 (result, errorMessage)。成功时 errorMessage 为空。
         */
        auto CancelShares(
            const QStringList& shareIds,
            QObject* ctx,
            CancelCallback cb
        ) -> void;

        /**
         * @brief 更新分享设置。
         * @param shareId      分享 ID 字符串。
         * @param expireDays   新的有效期天数（-1 = 不变，0 = 永久）。
         * @param password     新密码（空 = 移除密码，nullopt = 不变）。
         * @param permission   新权限（"view"/"download"，空 = 不变）。
         * @param ctx          QObject 生命周期守护。
         * @param cb           接收 (result, errorMessage)。成功时 errorMessage 为空。
         */
        auto UpdateShare(
            const QString& shareId,
            int expireDays,
            const std::optional<QString>& password,
            const QString& permission,
            QObject* ctx,
            UpdateCallback cb
        ) -> void;

    private:
        auto MapTransportError(const QString& networkError) const -> QString;
        auto MapEnvelopeError(const models::ApiEnvelope& envelope) const -> QString;

        api::ShareApi* m_share_api;
        TokenRefreshCoordinator* m_coordinator{ nullptr };
    };

} // namespace disk::qml::services
