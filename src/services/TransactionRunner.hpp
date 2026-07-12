/**
 * @file TransactionRunner.hpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 数据库事务运行器
 *
 * @copyright Copyright (c) 2026
 *
 */

#pragma once

#include <exception>
#include <memory>
#include <utility>

#include <drogon/orm/DbClient.h>

#include "utils/ErrorCode.hpp"
#include "utils/LogHelper.hpp"

namespace disk::file {

    /**
     * @brief 数据库事务运行器
     *
     * 仅负责数据库事务 begin/rollback 和错误映射；文件系统补偿保持在调用方显式处理。
     * 映射规则：callback 返回的 ErrorInfo 是公开领域错误，rollback 后原样返回；
     * DB/普通异常仅记录内部细节，对客户端统一返回默认 InternalError；rollback 失败只记录日志。
     */
    class TransactionRunner {
    public:
        explicit TransactionRunner(
            drogon::orm::DbClientPtr db_client,
            ErrorInfo default_error = ErrorInfo(ErrorCode::InternalError)
        )
            : m_db_client(std::move(db_client)),
              m_default_error(std::move(default_error)) {
        }

        template <typename Func>
        [[nodiscard]]
        auto Run(Func&& func) const -> drogon::Task<Result<void>> {
            std::shared_ptr<drogon::orm::Transaction> transaction;
            try {
                transaction = co_await m_db_client->newTransactionCoro();
                auto result = co_await std::forward<Func>(func)(transaction);
                if (!result) {
                    rollbackQuietly(transaction);
                    co_return std::unexpected(result.error());
                }
                co_return {};
            } catch (const drogon::orm::DrogonDbException& e) {
                rollbackQuietly(transaction);
                Logger::Error() << "Database transaction failed: " << e.base().what();
                co_return std::unexpected(m_default_error);
            } catch (const std::exception& e) {
                rollbackQuietly(transaction);
                Logger::Error() << "Database transaction failed: " << e.what();
                co_return std::unexpected(m_default_error);
            }
        }

    private:
        static auto rollbackQuietly(const std::shared_ptr<drogon::orm::Transaction>& transaction) -> void {
            if (!transaction) {
                return;
            }

            try {
                transaction->rollback();
            } catch (const std::exception& e) {
                Logger::Error() << "Transaction rollback failed: " << e.what();
            }
        }

        drogon::orm::DbClientPtr m_db_client;
        ErrorInfo m_default_error;
    };

} // namespace disk::file
