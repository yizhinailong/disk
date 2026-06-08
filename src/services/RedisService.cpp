/**
 * @file RedisService.cpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief Redis 服务实现
 *
 * @copyright Copyright (c) 2026
 *
 */

#include "RedisService.hpp"

#include <chrono>
#include <string_view>

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
            const std::exception& ex
        ) -> std::unexpected<ErrorInfo> {
            Logger::Error() << "Redis result parse failed: " << operation << ", error=" << ex.what();
            return std::unexpected(ErrorInfo(
                ErrorCode::RedisOperationFailed,
                "Redis operation failed: " + std::string(ex.what())
            ));
        }

    } ///< namespace

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

    auto RedisService::Set(const std::string& key, const std::string& value, int ttl)
        -> drogon::Task<Result<void>> {
        auto cmd_start = std::chrono::steady_clock::now();
        try {
            if (ttl > 0) {
                co_await m_redis_client
                    ->execCommandCoro("SETEX %s %d %s", key.c_str(), ttl, value.c_str());
            } else {
                co_await m_redis_client->execCommandCoro("SET %s %s", key.c_str(), value.c_str());
            }

            Logger::Debug() << "[redis_timer] SET duration_us="
                      << std::chrono::duration_cast<std::chrono::microseconds>(
                             std::chrono::steady_clock::now() - cmd_start
                         )
                             .count()
                      << " key=" << key;

            co_return {};

        } catch (const drogon::nosql::RedisException& ex) {
            Logger::Error() << "Redis operation failed: SET, key=" << key << ", error=" << ex.what();
            co_return std::unexpected(ErrorInfo(
                ErrorCode::RedisOperationFailed,
                "Redis operation failed: " + std::string(ex.what())
            ));
        }
    }

    auto RedisService::Get(const std::string& key) -> drogon::Task<Result<std::string>> {
        auto cmd_start = std::chrono::steady_clock::now();
        try {
            auto result = co_await m_redis_client->execCommandCoro("GET %s", key.c_str());

            Logger::Debug() << "[redis_timer] GET duration_us="
                      << std::chrono::duration_cast<std::chrono::microseconds>(
                             std::chrono::steady_clock::now() - cmd_start
                         )
                             .count()
                      << " key=" << key;

            if (result.isNil()) {
                co_return std::unexpected(
                    ErrorInfo(ErrorCode::RedisKeyNotFound, "Redis key not found: " + key)
                );
            }

            const auto value = result.asString();

            co_return value;

        } catch (const drogon::nosql::RedisException& ex) {
            Logger::Error() << "Redis operation failed: GET, key=" << key << ", error=" << ex.what();
            co_return std::unexpected(ErrorInfo(
                ErrorCode::RedisOperationFailed,
                "Redis operation failed: " + std::string(ex.what())
            ));
        }
    }

    auto RedisService::Delete(const std::string& key) -> drogon::Task<Result<void>> {
        auto cmd_start = std::chrono::steady_clock::now();
        try {
            co_await m_redis_client->execCommandCoro("DEL %s", key.c_str());

            Logger::Debug() << "[redis_timer] DEL duration_us="
                      << std::chrono::duration_cast<std::chrono::microseconds>(
                             std::chrono::steady_clock::now() - cmd_start
                         )
                             .count()
                      << " key=" << key;

            co_return {};

        } catch (const drogon::nosql::RedisException& ex) {
            Logger::Error() << "Redis operation failed: DEL, key=" << key << ", error=" << ex.what();
            co_return std::unexpected(ErrorInfo(
                ErrorCode::RedisOperationFailed,
                "Redis operation failed: " + std::string(ex.what())
            ));
        }
    }

    auto RedisService::Exists(const std::string& key) -> drogon::Task<bool> {
        try {
            auto result = co_await m_redis_client->execCommandCoro("EXISTS %s", key.c_str());
            const auto exists = result.asInteger();

            Logger::Trace() << "Redis EXISTS: key=" << key << ", exists=" << (exists == 1);

            co_return exists == 1;

        } catch (const drogon::nosql::RedisException& ex) {
            Logger::Error() << "Redis operation failed: EXISTS, key=" << key << ", error=" << ex.what();
            co_return false;
        }
    }

    auto RedisService::Expire(const std::string& key, int ttl) -> drogon::Task<Result<void>> {
        try {
            auto result =
                co_await m_redis_client->execCommandCoro("EXPIRE %s %d", key.c_str(), ttl);

            if (result.asInteger() == 0) {
                Logger::Trace() << "Redis EXPIRE: key=" << key << ", ttl=" << ttl;

                co_return std::unexpected(
                    ErrorInfo(ErrorCode::RedisKeyNotFound, "Redis key not found: " + key)
                );
            }

            Logger::Trace() << "Redis EXPIRE: key=" << key << ", ttl=" << ttl;

            co_return {};

        } catch (const drogon::nosql::RedisException& ex) {
            Logger::Error() << "Redis operation failed: EXPIRE, key=" << key << ", error=" << ex.what();
            co_return std::unexpected(ErrorInfo(
                ErrorCode::RedisOperationFailed,
                "Redis operation failed: " + std::string(ex.what())
            ));
        }
    }

    auto RedisService::MSet(const std::vector<KeyValue>& pairs, int ttl)
        -> drogon::Task<Result<void>> {
        if (pairs.empty()) {
            co_return {};
        }

        auto cmd_start = std::chrono::steady_clock::now();
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
                    Logger::Error() << "Redis transaction returned unexpected reply count: expected="
                              << pairs.size() << ", actual=" << exec_results.size();
                    co_return std::unexpected(ErrorInfo(
                        ErrorCode::RedisOperationFailed,
                        "Redis operation failed: unexpected transaction reply count"
                    ));
                }
            }

            Logger::Debug() << "[redis_timer] MSET duration_us="
                      << std::chrono::duration_cast<std::chrono::microseconds>(
                             std::chrono::steady_clock::now() - cmd_start
                         )
                             .count()
                      << " count=" << pairs.size();

            co_return {};

        } catch (const drogon::nosql::RedisException& ex) {
            Logger::Error() << "Redis operation failed: MSET, count=" << pairs.size()
                      << ", error=" << ex.what();
            co_return std::unexpected(ErrorInfo(
                ErrorCode::RedisOperationFailed,
                "Redis operation failed: " + std::string(ex.what())
            ));
        } catch (const std::exception& ex) {
            co_return WrapRedisResultParseError("MSET", ex);
        }
    }

    auto RedisService::MGet(const std::vector<std::string>& keys)
        -> drogon::Task<Result<std::vector<std::string>>> {
        if (keys.empty()) {
            co_return std::vector<std::string>{};
        }

        auto cmd_start = std::chrono::steady_clock::now();
        try {
            const auto command = BuildMultiKeyCommand("MGET", keys);
            auto result = co_await m_redis_client->execCommandCoro(command.c_str());

            std::vector<std::string> values;
            values.reserve(keys.size());
            const auto result_array = result.asArray();

            if (result_array.size() != keys.size()) {
                Logger::Error() << "Redis MGET returned unexpected value count: expected=" << keys.size()
                          << ", actual=" << result_array.size();
                co_return std::unexpected(ErrorInfo(
                    ErrorCode::RedisOperationFailed,
                    "Redis operation failed: unexpected MGET reply count"
                ));
            }

            for (size_t i = 0; i < result_array.size(); ++i) {
                values.push_back(result_array[i].isNil() ? "" : result_array[i].asString());
            }

            Logger::Debug() << "[redis_timer] MGET duration_us="
                      << std::chrono::duration_cast<std::chrono::microseconds>(
                             std::chrono::steady_clock::now() - cmd_start
                         )
                             .count()
                      << " count=" << keys.size();

            co_return values;

        } catch (const drogon::nosql::RedisException& ex) {
            Logger::Error() << "Redis operation failed: MGET, count=" << keys.size()
                      << ", error=" << ex.what();
            co_return std::unexpected(ErrorInfo(
                ErrorCode::RedisOperationFailed,
                "Redis operation failed: " + std::string(ex.what())
            ));
        } catch (const std::exception& ex) {
            co_return WrapRedisResultParseError("MGET", ex);
        }
    }

    auto RedisService::MDelete(const std::vector<std::string>& keys) -> drogon::Task<Result<int>> {
        if (keys.empty()) {
            co_return 0;
        }

        auto cmd_start = std::chrono::steady_clock::now();
        try {
            const auto command = BuildMultiKeyCommand("DEL", keys);
            auto result = co_await m_redis_client->execCommandCoro(command.c_str());
            const auto deleted = result.asInteger();

            Logger::Debug() << "[redis_timer] MDELETE duration_us="
                      << std::chrono::duration_cast<std::chrono::microseconds>(
                             std::chrono::steady_clock::now() - cmd_start
                         )
                             .count()
                      << " count=" << keys.size() << " deleted=" << deleted;

            co_return deleted;

        } catch (const drogon::nosql::RedisException& ex) {
            Logger::Error() << "Redis operation failed: MDELETE, count=" << keys.size()
                      << ", error=" << ex.what();
            co_return std::unexpected(ErrorInfo(
                ErrorCode::RedisOperationFailed,
                "Redis operation failed: " + std::string(ex.what())
            ));
        } catch (const std::exception& ex) {
            co_return WrapRedisResultParseError("MDELETE", ex);
        }
    }

    auto RedisService::Incr(const std::string& key) -> drogon::Task<Result<std::int64_t>> {
        try {
            auto result = co_await m_redis_client->execCommandCoro("INCR %s", key.c_str());
            const auto new_value = result.asInteger();

            Logger::Trace() << "Redis INCR: key=" << key << ", new_value=" << new_value;

            co_return new_value;

        } catch (const drogon::nosql::RedisException& ex) {
            Logger::Error() << "Redis operation failed: INCR, key=" << key << ", error=" << ex.what();
            co_return std::unexpected(ErrorInfo(
                ErrorCode::RedisOperationFailed,
                "Redis operation failed: " + std::string(ex.what())
            ));
        }
    }

    auto RedisService::IncrBy(const std::string& key, std::int64_t increment)
        -> drogon::Task<Result<std::int64_t>> {
        try {
            auto result =
                co_await m_redis_client->execCommandCoro("INCRBY %s %lld", key.c_str(), increment);
            const auto new_value = result.asInteger();

            Logger::Trace() << "Redis INCRBY: key=" << key << ", increment=" << increment
                      << ", new_value=" << new_value;

            co_return new_value;

        } catch (const drogon::nosql::RedisException& ex) {
            Logger::Error() << "Redis operation failed: INCRBY, key=" << key << ", error=" << ex.what();
            co_return std::unexpected(ErrorInfo(
                ErrorCode::RedisOperationFailed,
                "Redis operation failed: " + std::string(ex.what())
            ));
        }
    }

    auto RedisService::CompareAndSwap(
        const std::string& key,
        const std::string& expected,
        const std::string& new_value,
        int ttl
    ) -> drogon::Task<Result<bool>> {
        static constexpr char CAS_LUA_SCRIPT[] =
            "local current = redis.call('GET', KEYS[1]) " "if current == ARGV[1] then " "    redis.call('SET', KEYS[1], ARGV[2], 'EX', ARGV[3]) " "    return 1 " "else " "    return 0 " "end";

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

            Logger::Trace() << "Redis CAS: key=" << key << ", success=" << success;

            co_return success;

        } catch (const drogon::nosql::RedisException& ex) {
            Logger::Error() << "Redis operation failed: CAS, key=" << key << ", error=" << ex.what();
            co_return std::unexpected(ErrorInfo(
                ErrorCode::RedisOperationFailed,
                "Redis operation failed: " + std::string(ex.what())
            ));
        }
    }

    auto RedisService::IncrWithExpire(
        const std::string& key,
        int ttl
    ) -> drogon::Task<Result<std::int64_t>> {
        static constexpr char RATE_LIMIT_LUA_SCRIPT[] =
            "local count = redis.call('INCR', KEYS[1]) " "if count == 1 then " "    redis.call('EXPIRE', KEYS[1], ARGV[1]) " "end " "return count";

        try {
            auto result = co_await m_redis_client->execCommandCoro(
                "EVAL %s 1 %s %d",
                RATE_LIMIT_LUA_SCRIPT,
                key.c_str(),
                ttl
            );

            const auto count = result.asInteger();

            Logger::Trace() << "Redis IncrWithExpire: key=" << key << ", count=" << count;

            co_return count;

        } catch (const drogon::nosql::RedisException& ex) {
            Logger::Error() << "Redis operation failed: IncrWithExpire, key=" << key << ", error=" << ex.what();
            co_return std::unexpected(ErrorInfo(
                ErrorCode::RedisOperationFailed,
                "Redis operation failed: " + std::string(ex.what())
            ));
        }
    }

} ///< namespace disk::services
