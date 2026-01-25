/**
 * @file RedisService.cpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief Redis 服务实现
 * @version 0.1
 * @date 2026-01-21
 *
 * @copyright Copyright (c) 2026
 *
 */

#include "RedisService.hpp"

#include "utils/ErrorCode.hpp"

namespace disk::services {

    using disk::error::ErrorInfo;

    RedisService::RedisService(drogon::nosql::RedisClientPtr redis_client)
        : m_redis_client(std::move(redis_client)) {
        LOG_DEBUG << "RedisService 初始化成功";
    }

    // ==================== 通用方法实现 ====================

    auto RedisService::Set(const std::string& key, const std::string& value, int ttl)
        -> drogon::Task<Result<void>> {
        try {
            if (ttl > 0) {
                co_await m_redis_client->execCommandCoro(
                    "SETEX %s %d %s",
                    key.c_str(),
                    ttl,
                    value.c_str()
                );
            } else {
                co_await m_redis_client->execCommandCoro(
                    "SET %s %s",
                    key.c_str(),
                    value.c_str()
                );
            }

            LOG_DEBUG << "Redis SET: key=" << key << ", ttl=" << ttl;

            co_return {};

        } catch (const drogon::nosql::RedisException& ex) {
            LOG_ERROR << "Redis操作失败: SET, key=" << key << ", error=" << ex.what();
            co_return std::unexpected(ErrorInfo(
                ErrorCode::RedisOperationFailed,
                "Redis操作失败: " + std::string(ex.what())
            ));
        }
    }

    auto RedisService::Get(const std::string& key) -> drogon::Task<Result<std::string>> {
        try {
            auto result = co_await m_redis_client->execCommandCoro("GET %s", key.c_str());

            if (result.isNil()) {
                LOG_DEBUG << "Redis GET: key=" << key;

                co_return std::unexpected(ErrorInfo(
                    ErrorCode::RedisKeyNotFound,
                    "Redis键不存在: " + key
                ));
            }

            const auto value = result.asString();

            LOG_DEBUG << "Redis GET: key=" << key;

            co_return value;

        } catch (const drogon::nosql::RedisException& ex) {
            LOG_ERROR << "Redis操作失败: GET, key=" << key << ", error=" << ex.what();
            co_return std::unexpected(ErrorInfo(
                ErrorCode::RedisOperationFailed,
                "Redis操作失败: " + std::string(ex.what())
            ));
        }
    }

    auto RedisService::Delete(const std::string& key) -> drogon::Task<Result<void>> {
        try {
            co_await m_redis_client->execCommandCoro("DEL %s", key.c_str());

            LOG_DEBUG << "Redis DEL: key=" << key;

            co_return {};

        } catch (const drogon::nosql::RedisException& ex) {
            LOG_ERROR << "Redis操作失败: DEL, key=" << key << ", error=" << ex.what();
            co_return std::unexpected(ErrorInfo(
                ErrorCode::RedisOperationFailed,
                "Redis操作失败: " + std::string(ex.what())
            ));
        }
    }

    auto RedisService::Exists(const std::string& key) -> drogon::Task<bool> {
        try {
            auto result = co_await m_redis_client->execCommandCoro("EXISTS %s", key.c_str());
            const auto exists = result.asInteger();

            LOG_DEBUG << "Redis EXISTS: key=" << key << ", exists=" << (exists == 1);

            co_return exists == 1;

        } catch (const drogon::nosql::RedisException& ex) {
            LOG_ERROR << "Redis操作失败: EXISTS, key=" << key << ", error=" << ex.what();
            co_return false;
        }
    }

    auto RedisService::Expire(const std::string& key, int ttl)
        -> drogon::Task<Result<void>> {
        try {
            auto result = co_await m_redis_client->execCommandCoro("EXPIRE %s %d", key.c_str(), ttl);

            if (result.asInteger() == 0) {
                LOG_DEBUG << "Redis EXPIRE: key=" << key << ", ttl=" << ttl;

                co_return std::unexpected(ErrorInfo(ErrorCode::RedisKeyNotFound, "Redis键不存在: " + key));
            }

            LOG_DEBUG << "Redis EXPIRE: key=" << key << ", ttl=" << ttl;

            co_return {};

        } catch (const drogon::nosql::RedisException& ex) {
            LOG_ERROR << "Redis操作失败: EXPIRE, key=" << key << ", error=" << ex.what();
            co_return std::unexpected(ErrorInfo(
                ErrorCode::RedisOperationFailed,
                "Redis操作失败: " + std::string(ex.what())
            ));
        }
    }

    auto RedisService::MSet(const std::vector<KeyValue>& pairs, int ttl)
        -> drogon::Task<Result<void>> {
        if (pairs.empty()) {
            co_return {};
        }

        try {
            if (ttl == 0) {
                for (const auto& pair : pairs) {
                    co_await m_redis_client->execCommandCoro(
                        "SET %s %s",
                        pair.key.c_str(),
                        pair.value.c_str()
                    );
                }
            } else {
                co_await m_redis_client->execCommandCoro("MULTI");
                for (const auto& pair : pairs) {
                    co_await m_redis_client->execCommandCoro(
                        "SET %s %s EX %d",
                        pair.key.c_str(),
                        pair.value.c_str(),
                        ttl
                    );
                }
                co_await m_redis_client->execCommandCoro("EXEC");
            }

            LOG_DEBUG << "Redis MSET: count=" << pairs.size() << ", ttl=" << ttl;

            co_return {};

        } catch (const drogon::nosql::RedisException& ex) {
            LOG_ERROR << "Redis操作失败: MSET, count=" << pairs.size()
                      << ", error=" << ex.what();
            co_return std::unexpected(ErrorInfo(
                ErrorCode::RedisOperationFailed,
                "Redis操作失败: " + std::string(ex.what())
            ));
        }
    }

    auto RedisService::MGet(const std::vector<std::string>& keys)
        -> drogon::Task<Result<std::vector<std::string>>> {
        if (keys.empty()) {
            co_return std::vector<std::string>{};
        }

        try {
            auto result = co_await m_redis_client->execCommandCoro("MGET %s", keys[0].c_str());

            std::vector<std::string> values;
            values.reserve(keys.size());
            for (size_t i = 0; i < keys.size(); ++i) {
                values.push_back(result.asArray()[i].isNil() ? "" : result.asArray()[i].asString());
            }

            LOG_DEBUG << "Redis MGET: count=" << keys.size();

            co_return values;

        } catch (const drogon::nosql::RedisException& ex) {
            LOG_ERROR << "Redis操作失败: MGET, count=" << keys.size()
                      << ", error=" << ex.what();
            co_return std::unexpected(ErrorInfo(
                ErrorCode::RedisOperationFailed,
                "Redis操作失败: " + std::string(ex.what())
            ));
        }
    }

    auto RedisService::MDelete(const std::vector<std::string>& keys) -> drogon::Task<Result<int>> {
        if (keys.empty()) {
            co_return 0;
        }

        try {
            auto result = co_await m_redis_client->execCommandCoro("DEL %s", keys[0].c_str());
            const auto deleted = result.asInteger();

            LOG_DEBUG << "Redis MDELETE: count=" << keys.size() << ", deleted=" << deleted;

            co_return deleted;

        } catch (const drogon::nosql::RedisException& ex) {
            LOG_ERROR << "Redis操作失败: MDELETE, count=" << keys.size()
                      << ", error=" << ex.what();
            co_return std::unexpected(ErrorInfo(
                ErrorCode::RedisOperationFailed,
                "Redis操作失败: " + std::string(ex.what())
            ));
        }
    }

    auto RedisService::Incr(const std::string& key) -> drogon::Task<Result<std::int64_t>> {
        try {
            auto result = co_await m_redis_client->execCommandCoro(
                "INCR %s",
                key.c_str()
            );
            const auto new_value = result.asInteger();

            LOG_DEBUG << "Redis INCR: key=" << key << ", new_value=" << new_value;

            co_return new_value;

        } catch (const drogon::nosql::RedisException& ex) {
            LOG_ERROR << "Redis操作失败: INCR, key=" << key << ", error=" << ex.what();
            co_return std::unexpected(ErrorInfo(
                ErrorCode::RedisOperationFailed,
                "Redis操作失败: " + std::string(ex.what())
            ));
        }
    }

    auto RedisService::IncrBy(const std::string& key, std::int64_t increment)
        -> drogon::Task<Result<std::int64_t>> {
        try {
            auto result = co_await m_redis_client->execCommandCoro(
                "INCRBY %s %lld",
                key.c_str(),
                increment
            );
            const auto new_value = result.asInteger();

            LOG_DEBUG << "Redis INCRBY: key=" << key << ", increment=" << increment << ", new_value=" << new_value;

            co_return new_value;

        } catch (const drogon::nosql::RedisException& ex) {
            LOG_ERROR << "Redis操作失败: INCRBY, key=" << key << ", error=" << ex.what();
            co_return std::unexpected(ErrorInfo(
                ErrorCode::RedisOperationFailed,
                "Redis操作失败: " + std::string(ex.what())
            ));
        }
    }

} // namespace disk::services
