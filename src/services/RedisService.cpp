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
#include <vector>

#include "services/MetricsService.hpp"
#include "utils/ErrorCode.hpp"

namespace disk::services {

    namespace {

        auto BuildMultiKeyCommand(
            std::string_view command_name,
            const std::vector<std::string>& keys
        ) -> std::string {
            size_t command_size = command_name.size();
            for (const auto& key : keys) {
                command_size += 1 + key.size();
            }

            std::string command;
            command.reserve(command_size);
            command.append(command_name);

            for (const auto& key : keys) {
                command.push_back(' ');
                command.append(key);
            }

            return command;
        }

        auto BuildMSetCommand(const std::vector<KeyValue>& pairs) -> std::string {
            size_t command_size = 4;
            for (const auto& pair : pairs) {
                command_size += 2 + pair.key.size() + pair.value.size();
            }

            std::string command;
            command.reserve(command_size);
            command.append("MSET");

            for (const auto& pair : pairs) {
                command.push_back(' ');
                command.append(pair.key);
                command.push_back(' ');
                command.append(pair.value);
            }

            return command;
        }

        auto WrapRedisResultParseError(
            std::string_view operation,
            const std::exception& ex,
            disk::utils::LogContext log_context
        ) -> std::unexpected<ErrorInfo> {
            Logger::Error(log_context) << "Redis result parse failed: " << operation;
            return std::unexpected(ErrorInfo(
                ErrorCode::RedisOperationFailed,
                "Redis operation failed: " + std::string(ex.what())
            ));
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
            Logger::Debug() << "RedisService initialized";
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
            co_return std::unexpected(ErrorInfo(
                ErrorCode::RedisOperationFailed,
                "Redis operation failed: " + std::string(ex.what())
            ));
        } catch (const std::exception& ex) {
            timer.Finish(disk::metrics::DependencyOutcome::Protocol);
            co_return WrapRedisResultParseError("PING", ex, log_context);
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
            co_return std::unexpected(ErrorInfo(
                ErrorCode::RedisOperationFailed,
                "Redis operation failed: " + std::string(ex.what())
            ));
        }
    }

    auto RedisService::Get(const std::string& key, disk::utils::LogContext log_context)
        -> drogon::Task<Result<std::string>> {
        disk::metrics::DependencyCallTimer timer(disk::metrics::Dependency::Redis);
        try {
            auto result = co_await m_redis_client->execCommandCoro("GET %s", key.c_str());

            if (result.isNil()) {
                timer.Finish(disk::metrics::DependencyOutcome::Success);
                co_return std::unexpected(
                    ErrorInfo(ErrorCode::RedisKeyNotFound, "Redis key not found: " + key)
                );
            }

            const auto value = result.asString();

            timer.Finish(disk::metrics::DependencyOutcome::Success);
            co_return value;

        } catch (const drogon::nosql::RedisException& ex) {
            timer.Finish(ClassifyRedisError(ex.code()));
            Logger::Error(log_context) << "Redis operation failed: GET";
            co_return std::unexpected(ErrorInfo(
                ErrorCode::RedisOperationFailed,
                "Redis operation failed: " + std::string(ex.what())
            ));
        } catch (const std::exception& ex) {
            timer.Finish(disk::metrics::DependencyOutcome::Protocol);
            co_return WrapRedisResultParseError("GET", ex, log_context);
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
            co_return std::unexpected(ErrorInfo(
                ErrorCode::RedisOperationFailed,
                "Redis operation failed: " + std::string(ex.what())
            ));
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
            co_return std::unexpected(ErrorInfo(
                ErrorCode::RedisOperationFailed,
                "Redis operation failed: " + std::string(ex.what())
            ));
        } catch (const std::exception& ex) {
            timer.Finish(disk::metrics::DependencyOutcome::Protocol);
            co_return WrapRedisResultParseError("EXISTS", ex, log_context);
        }
    }

    auto RedisService::Expire(
        const std::string& key,
        int ttl,
        disk::utils::LogContext log_context
    ) -> drogon::Task<Result<void>> {
        disk::metrics::DependencyCallTimer timer(disk::metrics::Dependency::Redis);
        try {
            auto result =
                co_await m_redis_client->execCommandCoro("EXPIRE %s %d", key.c_str(), ttl);

            if (result.asInteger() == 0) {
                Logger::Trace(log_context) << "Redis EXPIRE: ttl=" << ttl << ", updated=false";

                timer.Finish(disk::metrics::DependencyOutcome::Success);
                co_return std::unexpected(
                    ErrorInfo(ErrorCode::RedisKeyNotFound, "Redis key not found: " + key)
                );
            }

            Logger::Trace(log_context) << "Redis EXPIRE: ttl=" << ttl << ", updated=true";

            timer.Finish(disk::metrics::DependencyOutcome::Success);
            co_return {};

        } catch (const drogon::nosql::RedisException& ex) {
            timer.Finish(ClassifyRedisError(ex.code()));
            Logger::Error(log_context) << "Redis operation failed: EXPIRE";
            co_return std::unexpected(ErrorInfo(
                ErrorCode::RedisOperationFailed,
                "Redis operation failed: " + std::string(ex.what())
            ));
        } catch (const std::exception& ex) {
            timer.Finish(disk::metrics::DependencyOutcome::Protocol);
            co_return WrapRedisResultParseError("EXPIRE", ex, log_context);
        }
    }

    auto RedisService::MSet(
        const std::vector<KeyValue>& pairs,
        int ttl,
        disk::utils::LogContext log_context
    )
        -> drogon::Task<Result<void>> {
        if (pairs.empty()) {
            co_return {};
        }

        disk::metrics::DependencyCallTimer timer(disk::metrics::Dependency::Redis);
        try {
            if (ttl == 0) {
                const auto command = BuildMSetCommand(pairs);
                co_await m_redis_client->execCommandCoro(command.c_str());
            } else {
                auto transaction = co_await m_redis_client->newTransactionCoro();
                for (const auto& pair : pairs) {
                    co_await transaction->execCommandCoro(
                        "SETEX %s %d %s",
                        pair.key.c_str(),
                        ttl,
                        pair.value.c_str()
                    );
                }

                auto exec_result = co_await transaction->executeCoro();
                const auto exec_results = exec_result.asArray();

                if (exec_results.size() != pairs.size()) {
                    timer.Finish(disk::metrics::DependencyOutcome::Protocol);
                    Logger::Error(log_context) << "Redis transaction returned unexpected reply count: expected="
                                               << pairs.size() << ", actual=" << exec_results.size();
                    co_return std::unexpected(ErrorInfo(
                        ErrorCode::RedisOperationFailed,
                        "Redis operation failed: unexpected transaction reply count"
                    ));
                }
            }

            timer.Finish(disk::metrics::DependencyOutcome::Success);
            co_return {};

        } catch (const drogon::nosql::RedisException& ex) {
            timer.Finish(ClassifyRedisError(ex.code()));
            Logger::Error(log_context) << "Redis operation failed: MSET, count=" << pairs.size();
            co_return std::unexpected(ErrorInfo(
                ErrorCode::RedisOperationFailed,
                "Redis operation failed: " + std::string(ex.what())
            ));
        } catch (const std::exception& ex) {
            timer.Finish(disk::metrics::DependencyOutcome::Protocol);
            co_return WrapRedisResultParseError("MSET", ex, log_context);
        }
    }

    auto RedisService::MGet(
        const std::vector<std::string>& keys,
        disk::utils::LogContext log_context
    )
        -> drogon::Task<Result<std::vector<std::string>>> {
        if (keys.empty()) {
            co_return std::vector<std::string>{};
        }

        disk::metrics::DependencyCallTimer timer(disk::metrics::Dependency::Redis);
        try {
            const auto command = BuildMultiKeyCommand("MGET", keys);
            auto result = co_await m_redis_client->execCommandCoro(command.c_str());

            std::vector<std::string> values;
            values.reserve(keys.size());
            const auto result_array = result.asArray();

            if (result_array.size() != keys.size()) {
                timer.Finish(disk::metrics::DependencyOutcome::Protocol);
                Logger::Error(log_context) << "Redis MGET returned unexpected value count: expected=" << keys.size()
                                           << ", actual=" << result_array.size();
                co_return std::unexpected(ErrorInfo(
                    ErrorCode::RedisOperationFailed,
                    "Redis operation failed: unexpected MGET reply count"
                ));
            }

            for (size_t i = 0; i < result_array.size(); ++i) {
                values.push_back(result_array[i].isNil() ? "" : result_array[i].asString());
            }

            timer.Finish(disk::metrics::DependencyOutcome::Success);
            co_return values;

        } catch (const drogon::nosql::RedisException& ex) {
            timer.Finish(ClassifyRedisError(ex.code()));
            Logger::Error(log_context) << "Redis operation failed: MGET, count=" << keys.size();
            co_return std::unexpected(ErrorInfo(
                ErrorCode::RedisOperationFailed,
                "Redis operation failed: " + std::string(ex.what())
            ));
        } catch (const std::exception& ex) {
            timer.Finish(disk::metrics::DependencyOutcome::Protocol);
            co_return WrapRedisResultParseError("MGET", ex, log_context);
        }
    }

    auto RedisService::MDelete(
        const std::vector<std::string>& keys,
        disk::utils::LogContext log_context
    ) -> drogon::Task<Result<int>> {
        if (keys.empty()) {
            co_return 0;
        }

        disk::metrics::DependencyCallTimer timer(disk::metrics::Dependency::Redis);
        try {
            const auto command = BuildMultiKeyCommand("DEL", keys);
            auto result = co_await m_redis_client->execCommandCoro(command.c_str());
            const auto deleted = result.asInteger();

            timer.Finish(disk::metrics::DependencyOutcome::Success);
            co_return deleted;

        } catch (const drogon::nosql::RedisException& ex) {
            timer.Finish(ClassifyRedisError(ex.code()));
            Logger::Error(log_context) << "Redis operation failed: MDELETE, count=" << keys.size();
            co_return std::unexpected(ErrorInfo(
                ErrorCode::RedisOperationFailed,
                "Redis operation failed: " + std::string(ex.what())
            ));
        } catch (const std::exception& ex) {
            timer.Finish(disk::metrics::DependencyOutcome::Protocol);
            co_return WrapRedisResultParseError("MDELETE", ex, log_context);
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
            co_return std::unexpected(ErrorInfo(
                ErrorCode::RedisOperationFailed,
                "Redis operation failed: " + std::string(ex.what())
            ));
        } catch (const std::exception& ex) {
            timer.Finish(disk::metrics::DependencyOutcome::Protocol);
            co_return WrapRedisResultParseError("INCR", ex, log_context);
        }
    }

    auto RedisService::IncrBy(
        const std::string& key,
        std::int64_t increment,
        disk::utils::LogContext log_context
    )
        -> drogon::Task<Result<std::int64_t>> {
        disk::metrics::DependencyCallTimer timer(disk::metrics::Dependency::Redis);
        try {
            auto result =
                co_await m_redis_client->execCommandCoro("INCRBY %s %lld", key.c_str(), increment);
            const auto new_value = result.asInteger();

            Logger::Trace(log_context) << "Redis INCRBY: increment=" << increment;

            timer.Finish(disk::metrics::DependencyOutcome::Success);
            co_return new_value;

        } catch (const drogon::nosql::RedisException& ex) {
            timer.Finish(ClassifyRedisError(ex.code()));
            Logger::Error(log_context) << "Redis operation failed: INCRBY";
            co_return std::unexpected(ErrorInfo(
                ErrorCode::RedisOperationFailed,
                "Redis operation failed: " + std::string(ex.what())
            ));
        } catch (const std::exception& ex) {
            timer.Finish(disk::metrics::DependencyOutcome::Protocol);
            co_return WrapRedisResultParseError("INCRBY", ex, log_context);
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
            co_return std::unexpected(ErrorInfo(
                ErrorCode::RedisOperationFailed,
                "Redis operation failed: " + std::string(ex.what())
            ));
        } catch (const std::exception& ex) {
            timer.Finish(disk::metrics::DependencyOutcome::Protocol);
            co_return WrapRedisResultParseError("CAS", ex, log_context);
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
            co_return std::unexpected(ErrorInfo(
                ErrorCode::RedisOperationFailed,
                "Redis operation failed: " + std::string(ex.what())
            ));
        } catch (const std::exception& ex) {
            timer.Finish(disk::metrics::DependencyOutcome::Protocol);
            co_return WrapRedisResultParseError("IncrWithExpire", ex, log_context);
        }
    }

} // namespace disk::services
