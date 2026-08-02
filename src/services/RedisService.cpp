/**
 * @file RedisService.cpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief Redis 服务实现
 *
 * @copyright Copyright (c) 2026
 *
 */

#include "RedisService.hpp"

#include <string_view>

#include "services/MetricsService.hpp"
#include "utils/ErrorCode.hpp"

namespace disk::services {

    namespace {

        auto WrapRedisResultParseError(
            std::string_view operation,
            disk::utils::LogContext log_context
        ) -> std::unexpected<ErrorInfo> {
            Logger::Error(log_context) << "Redis result parse failed: " << operation;
            return std::unexpected(ErrorInfo(ErrorCode::RedisOperationFailed));
        }

        [[nodiscard]] auto ClassifyRedisError(drogon::nosql::RedisErrorCode code) noexcept
            -> disk::metrics::DependencyOutcome {
            using drogon::nosql::RedisErrorCode;
            switch (code) {
                case RedisErrorCode::kTimeout:
                    return disk::metrics::DependencyOutcome::Timeout;
                case RedisErrorCode::kConnectionBroken:
                case RedisErrorCode::kNoConnectionAvailable:
                    return disk::metrics::DependencyOutcome::Connection;
                case RedisErrorCode::kTransactionCancelled:
                    return disk::metrics::DependencyOutcome::Retryable;
                case RedisErrorCode::kRedisError:
                    return disk::metrics::DependencyOutcome::Permanent;
                case RedisErrorCode::kBadType:
                    return disk::metrics::DependencyOutcome::Protocol;
                case RedisErrorCode::kNone:
                case RedisErrorCode::kUnknown:
                case RedisErrorCode::kInternalError:
                    return disk::metrics::DependencyOutcome::Other;
            }
            return disk::metrics::DependencyOutcome::Other;
        }

    } // namespace

    using disk::error::ErrorInfo;

    /// ==================== 单例初始化 ====================

    auto RedisService::Initialize(drogon::nosql::RedisClientPtr redis_client) -> void {
        auto instance = GetInstance();
        if (!instance->m_redis_client) {
            instance->m_redis_client = std::move(redis_client);
            Logger::Debug(disk::utils::ServiceRuntimeLogContext()) << "Service initialized: service=redis";
        }
    }

    /// ==================== 通用方法实现 ====================

    auto RedisService::Ping(disk::utils::LogContext log_context) -> drogon::Task<Result<bool>> {
        disk::metrics::DependencyCallTimer timer(disk::metrics::Dependency::Redis);
        try {
            const auto result = co_await m_redis_client->execCommandCoro("PING");
            if (result.isNil()) {
                timer.Finish(disk::metrics::DependencyOutcome::Protocol);
                co_return false;
            }
            const auto pong = result.asString() == "PONG";
            timer.Finish(
                pong ? disk::metrics::DependencyOutcome::Success :
                       disk::metrics::DependencyOutcome::Protocol
            );
            co_return pong;
        } catch (const drogon::nosql::RedisException& ex) {
            timer.Finish(ClassifyRedisError(ex.code()));
            Logger::Error(log_context) << "Redis operation failed: PING";
            co_return std::unexpected(ErrorInfo(ErrorCode::RedisOperationFailed));
        } catch (const std::exception&) {
            timer.Finish(disk::metrics::DependencyOutcome::Protocol);
            co_return WrapRedisResultParseError("PING", log_context);
        }
    }

    auto RedisService::Set(
        const std::string& key,
        const std::string& value,
        int ttl,
        disk::utils::LogContext log_context
    )
        -> drogon::Task<Result<void>> {
        disk::metrics::DependencyCallTimer timer(disk::metrics::Dependency::Redis);
        try {
            if (ttl > 0) {
                co_await m_redis_client
                    ->execCommandCoro("SETEX %s %d %s", key.c_str(), ttl, value.c_str());
            } else {
                co_await m_redis_client->execCommandCoro("SET %s %s", key.c_str(), value.c_str());
            }

            timer.Finish(disk::metrics::DependencyOutcome::Success);
            co_return {};

        } catch (const drogon::nosql::RedisException& ex) {
            timer.Finish(ClassifyRedisError(ex.code()));
            Logger::Error(log_context) << "Redis operation failed: SET";
            co_return std::unexpected(ErrorInfo(ErrorCode::RedisOperationFailed));
        }
    }

    auto RedisService::Get(const std::string& key, disk::utils::LogContext log_context)
        -> drogon::Task<Result<std::string>> {
        disk::metrics::DependencyCallTimer timer(disk::metrics::Dependency::Redis);
        try {
            auto result = co_await m_redis_client->execCommandCoro("GET %s", key.c_str());

            if (result.isNil()) {
                timer.Finish(disk::metrics::DependencyOutcome::Success);
                co_return std::unexpected(ErrorInfo(ErrorCode::RedisKeyNotFound));
            }

            const auto value = result.asString();

            timer.Finish(disk::metrics::DependencyOutcome::Success);
            co_return value;

        } catch (const drogon::nosql::RedisException& ex) {
            timer.Finish(ClassifyRedisError(ex.code()));
            Logger::Error(log_context) << "Redis operation failed: GET";
            co_return std::unexpected(ErrorInfo(ErrorCode::RedisOperationFailed));
        } catch (const std::exception&) {
            timer.Finish(disk::metrics::DependencyOutcome::Protocol);
            co_return WrapRedisResultParseError("GET", log_context);
        }
    }

    auto RedisService::Delete(const std::string& key, disk::utils::LogContext log_context)
        -> drogon::Task<Result<void>> {
        disk::metrics::DependencyCallTimer timer(disk::metrics::Dependency::Redis);
        try {
            co_await m_redis_client->execCommandCoro("DEL %s", key.c_str());
            timer.Finish(disk::metrics::DependencyOutcome::Success);
            co_return {};

        } catch (const drogon::nosql::RedisException& ex) {
            timer.Finish(ClassifyRedisError(ex.code()));
            Logger::Error(log_context) << "Redis operation failed: DEL";
            co_return std::unexpected(ErrorInfo(ErrorCode::RedisOperationFailed));
        }
    }

    auto RedisService::Exists(const std::string& key, disk::utils::LogContext log_context)
        -> drogon::Task<Result<bool>> {
        disk::metrics::DependencyCallTimer timer(disk::metrics::Dependency::Redis);
        try {
            auto result = co_await m_redis_client->execCommandCoro("EXISTS %s", key.c_str());
            const auto exists = result.asInteger();

            Logger::Trace(log_context) << "Redis EXISTS: exists=" << (exists == 1);

            timer.Finish(disk::metrics::DependencyOutcome::Success);
            co_return exists == 1;

        } catch (const drogon::nosql::RedisException& ex) {
            timer.Finish(ClassifyRedisError(ex.code()));
            Logger::Error(log_context) << "Redis operation failed: EXISTS";
            co_return std::unexpected(ErrorInfo(ErrorCode::RedisOperationFailed));
        } catch (const std::exception&) {
            timer.Finish(disk::metrics::DependencyOutcome::Protocol);
            co_return WrapRedisResultParseError("EXISTS", log_context);
        }
    }

    auto RedisService::Incr(const std::string& key, disk::utils::LogContext log_context)
        -> drogon::Task<Result<std::int64_t>> {
        disk::metrics::DependencyCallTimer timer(disk::metrics::Dependency::Redis);
        try {
            auto result = co_await m_redis_client->execCommandCoro("INCR %s", key.c_str());
            const auto new_value = result.asInteger();

            Logger::Trace(log_context) << "Redis INCR succeeded";

            timer.Finish(disk::metrics::DependencyOutcome::Success);
            co_return new_value;

        } catch (const drogon::nosql::RedisException& ex) {
            timer.Finish(ClassifyRedisError(ex.code()));
            Logger::Error(log_context) << "Redis operation failed: INCR";
            co_return std::unexpected(ErrorInfo(ErrorCode::RedisOperationFailed));
        } catch (const std::exception&) {
            timer.Finish(disk::metrics::DependencyOutcome::Protocol);
            co_return WrapRedisResultParseError("INCR", log_context);
        }
    }

    auto RedisService::CompareAndSwap(
        const std::string& key,
        const std::string& expected,
        const std::string& new_value,
        int ttl,
        disk::utils::LogContext log_context
    ) -> drogon::Task<Result<bool>> {
        static constexpr char CAS_LUA_SCRIPT[] =
            "local current = redis.call('GET', KEYS[1]) " "if current == ARGV[1] then " "    redis.call('SET', KEYS[1], ARGV[2], 'EX', ARGV[3]) " "    return 1 " "else " "    return 0 " "end";

        disk::metrics::DependencyCallTimer timer(disk::metrics::Dependency::Redis);
        try {
            auto result = co_await m_redis_client->execCommandCoro(
                "EVAL %s 1 %s %s %s %d",
                CAS_LUA_SCRIPT,
                key.c_str(),
                expected.c_str(),
                new_value.c_str(),
                ttl
            );

            const auto success = result.asInteger() == 1;

            Logger::Trace(log_context) << "Redis CAS: success=" << success;

            timer.Finish(disk::metrics::DependencyOutcome::Success);
            co_return success;

        } catch (const drogon::nosql::RedisException& ex) {
            timer.Finish(ClassifyRedisError(ex.code()));
            Logger::Error(log_context) << "Redis operation failed: CAS";
            co_return std::unexpected(ErrorInfo(ErrorCode::RedisOperationFailed));
        } catch (const std::exception&) {
            timer.Finish(disk::metrics::DependencyOutcome::Protocol);
            co_return WrapRedisResultParseError("CAS", log_context);
        }
    }

    auto RedisService::IncrWithExpire(
        const std::string& key,
        int ttl,
        disk::utils::LogContext log_context
    ) -> drogon::Task<Result<std::int64_t>> {
        static constexpr char RATE_LIMIT_LUA_SCRIPT[] =
            "local count = redis.call('INCR', KEYS[1]) " "if count == 1 then " "    redis.call('EXPIRE', KEYS[1], ARGV[1]) " "end " "return count";

        disk::metrics::DependencyCallTimer timer(disk::metrics::Dependency::Redis);
        try {
            auto result = co_await m_redis_client->execCommandCoro(
                "EVAL %s 1 %s %d",
                RATE_LIMIT_LUA_SCRIPT,
                key.c_str(),
                ttl
            );

            const auto count = result.asInteger();

            Logger::Trace(log_context) << "Redis IncrWithExpire succeeded: count=" << count;

            timer.Finish(disk::metrics::DependencyOutcome::Success);
            co_return count;

        } catch (const drogon::nosql::RedisException& ex) {
            timer.Finish(ClassifyRedisError(ex.code()));
            Logger::Error(log_context) << "Redis operation failed: IncrWithExpire";
            co_return std::unexpected(ErrorInfo(ErrorCode::RedisOperationFailed));
        } catch (const std::exception&) {
            timer.Finish(disk::metrics::DependencyOutcome::Protocol);
            co_return WrapRedisResultParseError("IncrWithExpire", log_context);
        }
    }

} // namespace disk::services
