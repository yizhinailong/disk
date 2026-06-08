/**
 * @file AuthCpuPool.hpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief Shared utility for running CPU-intensive auth work on the dedicated AuthCpuPool
 *
 * @copyright Copyright (c) 2026
 *
 */

#pragma once

#include <functional>
#include <type_traits>

#include <drogon/utils/coroutine.h>

#include "services/TokenService.hpp"

namespace disk::utils {

/**
 * @brief Run a callable on the AuthCpuPool and return the result.
 *
 * Submits @p func to the AuthCpuPool event loop via queueInLoopCoro,
 * then resumes the caller on its original event loop (if different).
 *
 * @tparam Func  Callable type
 * @param func   Callable to execute on the pool
 * @return drogon::Task<ReturnType>  Awaitable result of the callable
 */
template <typename Func>
auto RunOnAuthCpuPool(Func func)
    -> drogon::Task<std::remove_cvref_t<std::invoke_result_t<Func&>>> {
    using ReturnType = std::remove_cvref_t<std::invoke_result_t<Func&>>;

    auto* resume_loop = trantor::EventLoop::getEventLoopOfCurrentThread();
    auto result = co_await drogon::queueInLoopCoro<ReturnType>(
        disk::services::detail::GetAuthCpuWorkLoop(),
        std::function<ReturnType()>([func = std::move(func)]() mutable -> ReturnType {
            return func();
        })
    );

    if (resume_loop != nullptr &&
        resume_loop != trantor::EventLoop::getEventLoopOfCurrentThread()) {
        co_await drogon::switchThreadCoro(resume_loop);
    }

    co_return result;
}

} ///< namespace disk::utils
