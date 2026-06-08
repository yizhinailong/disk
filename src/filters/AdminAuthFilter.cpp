/**
 * @file AdminAuthFilter.cpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 管理员权限过滤器实现
 *
 * @copyright Copyright (c) 2026
 *
 */

#include "AdminAuthFilter.hpp"

#include <drogon/utils/coroutine.h>

#include "utils/Response.hpp"

namespace disk::filters {

    auto AdminAuthFilter::doFilter(const drogon::HttpRequestPtr& request)
        -> drogon::Task<drogon::HttpResponsePtr> {

        auto path = request->getPath();

        if (!path.starts_with("/api/admin/")) {
            co_return nullptr;
        }

        auto user_id = request->attributes()->get<uint64_t>("user_id");
        auto role = request->attributes()->get<int>("role");
        auto status = request->attributes()->get<int>("status");

        if (role != 1) {
            Logger::Warn() << "[admin_auth_filter] Non-admin access attempt: user_id=" << user_id
                     << " role=" << role << " path=" << path;
            co_return disk::Response::Error(disk::error::Code::AdminRequired);
        }

        if (status != 1) {
            Logger::Warn() << "[admin_auth_filter] Disabled admin access: user_id=" << user_id
                     << " status=" << status << " path=" << path;
            co_return disk::Response::Error(disk::error::Code::AdminRequired);
        }

        Logger::Trace() << "[admin_auth_filter] Admin access granted: user_id=" << user_id
                  << " path=" << path;

        co_return nullptr;
    }

} ///< namespace disk::filters
