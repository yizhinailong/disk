/**
 * @file TrashService.hpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief QML 客户端高级回收站编排服务
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

#include <dtos/TrashDtos.hpp>

namespace disk::qml::api {
    class TrashApi;
}

namespace disk::qml::services {
    class TokenRefreshCoordinator;

    /**
     * @brief QML 客户端回收站服务。
     * @details 编排回收站相关业务流程：
     *   - 分页列出回收站项目
     *   - 批量恢复回收站项目
     *   - 批量彻底删除回收站项目
     *   - 清空回收站
     *   - 委托 api::TrashApi 执行网络请求
     *   - 将传输层和响应封装层错误映射为用户友好消息
     */
    class TrashService final {
    public:
        using ListCallback = std::function<void(std::optional<models::TrashListResultDto> result, QString errorMessage)>;
        using RestoreCallback = std::function<void(std::optional<models::TrashBatchResultDto> result, QString errorMessage)>;
        using DeleteCallback = std::function<void(std::optional<models::TrashBatchResultDto> result, QString errorMessage)>;
        using ClearAllCallback = std::function<void(std::optional<models::TrashClearResultDto> result, QString errorMessage)>;

        explicit TrashService(api::TrashApi* trashApi, TokenRefreshCoordinator* coordinator = nullptr);

        /**
         * @brief 分页列出回收站项目。
         * @param page      页码（从 1 开始）。
         * @param pageSize  每页条数（1-100）。
         * @param ctx       QObject 生命周期守护。
         * @param cb        接收 (result, errorMessage)。成功时 errorMessage 为空。
         */
        auto ListTrash(
            int page,
            int pageSize,
            QObject* ctx,
            ListCallback cb
        ) -> void;

        /**
         * @brief 恢复回收站项目。
         * @details 调用 api::TrashApi::Restore 前验证 trashIds 非空。
         * @param trashIds  要恢复的回收站项目 ID 列表。
         * @param ctx       QObject 生命周期守护。
         * @param cb        接收 (result, errorMessage)。成功时 errorMessage 为空。
         */
        auto RestoreItems(const QList<qint64>& trashIds, QObject* ctx, RestoreCallback cb) -> void;

        /**
         * @brief 彻底删除回收站项目。
         * @details 调用 api::TrashApi::Delete 前验证 trashIds 非空。
         * @param trashIds  要彻底删除的回收站项目 ID 列表。
         * @param ctx       QObject 生命周期守护。
         * @param cb        接收 (result, errorMessage)。成功时 errorMessage 为空。
         */
        auto DeleteItems(const QList<qint64>& trashIds, QObject* ctx, DeleteCallback cb) -> void;

        /**
         * @brief 清空回收站（删除所有项目）。
         * @param ctx  QObject 生命周期守护。
         * @param cb   接收 (result, errorMessage)。成功时 errorMessage 为空。
         */
        auto ClearAll(QObject* ctx, ClearAllCallback cb) -> void;

    private:
        auto MapTransportError(const QString& networkError) const -> QString;
        auto MapEnvelopeError(const models::ApiEnvelope& envelope) const -> QString;

        api::TrashApi* m_trash_api;
        TokenRefreshCoordinator* m_coordinator{ nullptr };
    };

} // namespace disk::qml::services
