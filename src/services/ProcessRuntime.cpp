/**
 * @file ProcessRuntime.cpp
 * @brief 进程角色、就绪与排空状态实现
 */

#include "services/ProcessRuntime.hpp"

#include <stdexcept>
#include <utility>

namespace disk::runtime {

    std::shared_ptr<ProcessRuntimeState> ProcessRuntimeMgr::s_state;

    ProcessRuntimeState::ProcessRuntimeState(
        disk::utils::ProcessRole role,
        std::string instance_id,
        bool worker_claiming_enabled
    ) : m_role(role),
        m_instance_id(std::move(instance_id)),
        m_worker_claiming_enabled(
            disk::utils::IncludesWorker(role) && worker_claiming_enabled
        ) {
        if (m_instance_id.empty() || m_instance_id.size() > 128) {
            throw std::invalid_argument(
                "Process runtime instance ID must contain 1 to 128 characters"
            );
        }
    }

    auto ProcessRuntimeState::MarkInitialized() noexcept -> void {
        m_initialized.store(true);
    }

    auto ProcessRuntimeState::SetWorkerAccepting(bool accepting) noexcept -> void {
        m_worker_accepting.store(
            accepting && m_worker_claiming_enabled && !m_draining.load()
        );
    }

    auto ProcessRuntimeState::BeginDrain() noexcept -> bool {
        const auto was_draining = m_draining.exchange(true);
        m_worker_accepting.store(false);
        return !was_draining;
    }

    auto ProcessRuntimeState::TryAcquireBusinessRequest() noexcept -> bool {
        if (!CanAcceptBusinessRequest()) {
            return false;
        }

        m_business_requests_inflight.fetch_add(1);
        if (CanAcceptBusinessRequest()) {
            return true;
        }

        ReleaseBusinessRequest();
        return false;
    }

    auto ProcessRuntimeState::ReleaseBusinessRequest() noexcept -> void {
        auto current = m_business_requests_inflight.load();
        while (current > 0 && !m_business_requests_inflight.compare_exchange_weak(
                                  current,
                                  current - 1
                              )) {
        }
    }

    auto ProcessRuntimeState::IsInitialized() const noexcept -> bool {
        return m_initialized.load();
    }

    auto ProcessRuntimeState::IsDraining() const noexcept -> bool {
        return m_draining.load();
    }

    auto ProcessRuntimeState::IsWorkerAccepting() const noexcept -> bool {
        return m_worker_accepting.load();
    }

    auto ProcessRuntimeState::IsWorkerClaimingEnabled() const noexcept -> bool {
        return m_worker_claiming_enabled;
    }

    auto ProcessRuntimeState::IsReady() const noexcept -> bool {
        if (!m_initialized.load() || m_draining.load()) {
            return false;
        }
        return !m_worker_claiming_enabled || m_worker_accepting.load();
    }

    auto ProcessRuntimeState::BusinessRequestsInflight() const noexcept -> size_t {
        return m_business_requests_inflight.load();
    }

    auto ProcessRuntimeState::IsApiDrained() const noexcept -> bool {
        return m_business_requests_inflight.load() == 0;
    }

    auto ProcessRuntimeState::Role() const noexcept -> disk::utils::ProcessRole {
        return m_role;
    }

    auto ProcessRuntimeState::InstanceId() const noexcept -> const std::string& {
        return m_instance_id;
    }

    auto ProcessRuntimeState::CanAcceptBusinessRequest() const noexcept -> bool {
        return disk::utils::IncludesApi(m_role) && m_initialized.load() && !m_draining.load();
    }

    auto ProcessRuntimeMgr::SetInstance(std::shared_ptr<ProcessRuntimeState> state) -> void {
        if (state == nullptr) {
            throw std::invalid_argument("Process runtime state is required");
        }
        if (s_state != nullptr) {
            throw std::logic_error("Process runtime state is already initialized");
        }
        s_state = std::move(state);
    }

    auto ProcessRuntimeMgr::GetInstance() -> std::shared_ptr<ProcessRuntimeState> {
        if (s_state == nullptr) {
            throw std::logic_error("Process runtime state is not initialized");
        }
        return s_state;
    }

    auto ProcessRuntimeMgr::IsInitialized() noexcept -> bool {
        return s_state != nullptr;
    }

} // namespace disk::runtime
