/**
 * @file ProcessRuntime.hpp
 * @brief 进程角色、就绪与排空状态
 */

#pragma once

#include <atomic>
#include <cstddef>
#include <memory>
#include <string>
#include <string_view>

#include "utils/ConfigMgr.hpp"

namespace disk::runtime {

    [[nodiscard]] constexpr auto IsHealthProbePath(std::string_view path) noexcept -> bool {
        return path == "/api/health" || path == "/api/health/live" ||
               path == "/api/health/ready";
    }

    [[nodiscard]] constexpr auto IsInternalOperationalPath(std::string_view path) noexcept
        -> bool {
        return IsHealthProbePath(path) || path == "/metrics";
    }

    class ProcessRuntimeState final {
    public:
        ProcessRuntimeState(disk::utils::ProcessRole role, std::string instance_id);

        auto MarkInitialized() noexcept -> void;

        auto SetWorkerAccepting(bool accepting) noexcept -> void;

        [[nodiscard]]
        auto BeginDrain() noexcept -> bool;

        [[nodiscard]]
        auto TryAcquireBusinessRequest() noexcept -> bool;

        auto ReleaseBusinessRequest() noexcept -> void;

        [[nodiscard]]
        auto IsInitialized() const noexcept -> bool;

        [[nodiscard]]
        auto IsDraining() const noexcept -> bool;

        [[nodiscard]]
        auto IsWorkerAccepting() const noexcept -> bool;

        [[nodiscard]]
        auto IsReady() const noexcept -> bool;

        [[nodiscard]]
        auto BusinessRequestsInflight() const noexcept -> size_t;

        [[nodiscard]]
        auto IsApiDrained() const noexcept -> bool;

        [[nodiscard]]
        auto Role() const noexcept -> disk::utils::ProcessRole;

        [[nodiscard]]
        auto InstanceId() const noexcept -> const std::string&;

    private:
        [[nodiscard]]
        auto CanAcceptBusinessRequest() const noexcept -> bool;

        disk::utils::ProcessRole m_role;
        std::string m_instance_id;
        std::atomic_bool m_initialized{ false };
        std::atomic_bool m_draining{ false };
        std::atomic_bool m_worker_accepting{ false };
        std::atomic_size_t m_business_requests_inflight{ 0 };
    };

    class ProcessRuntimeMgr final {
    public:
        static auto SetInstance(std::shared_ptr<ProcessRuntimeState> state) -> void;

        [[nodiscard]]
        static auto GetInstance() -> std::shared_ptr<ProcessRuntimeState>;

        [[nodiscard]]
        static auto IsInitialized() noexcept -> bool;

    private:
        static std::shared_ptr<ProcessRuntimeState> s_state;
    };

} // namespace disk::runtime
