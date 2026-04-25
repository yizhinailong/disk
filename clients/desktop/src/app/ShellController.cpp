#include "ShellController.hpp"

#include "auth/OwnerSessionManager.hpp"
#include "auth/SessionStore.hpp"
#include "auth/VisitorSessionManager.hpp"

namespace disk::app {

    ShellController::ShellController(disk::desktop::SessionStore* session_store, QObject* parent)
        : QObject(parent), m_session_store(session_store), m_current_shell("splash"), m_page_state("loading") {
        if (m_session_store) {
            connect(m_session_store->GetOwnerManager(), &disk::desktop::OwnerSessionManager::stateChanged, this, &ShellController::onOwnerSessionStateChanged);
            connect(m_session_store->GetVisitorManager(), &disk::desktop::VisitorSessionManager::stateChanged, this, &ShellController::onVisitorSessionStateChanged);
        }
    }

    void ShellController::Initialize() {
        if (!m_session_store) {
            return;
        }

        auto owner_state = m_session_store->GetOwnerManager()->GetState();
        if (owner_state == disk::desktop::OwnerSessionState::Active ||
            owner_state == disk::desktop::OwnerSessionState::Refreshing) {
            navigateToOwner();
        } else {
            navigateToLogin();
        }
    }

    QString ShellController::GetCurrentShell() const {
        return m_current_shell;
    }

    QString ShellController::GetPageState() const {
        return m_page_state;
    }

    void ShellController::navigateToOwner() {
        if (!m_session_store) {
            return;
        }

        auto owner_state = m_session_store->GetOwnerManager()->GetState();
        if (owner_state == disk::desktop::OwnerSessionState::Active ||
            owner_state == disk::desktop::OwnerSessionState::Refreshing) {
            if (m_current_shell != "owner") {
                m_current_shell = "owner";
                emit currentShellChanged();
            }
        } else {
            navigateToLogin();
        }
    }

    void ShellController::navigateToVisitor(const QString& shareId) {
        if (!m_session_store) {
            return;
        }

        m_session_store->ActivateVisitor(shareId);
        if (m_current_shell != "visitor") {
            m_current_shell = "visitor";
            emit currentShellChanged();
        }
    }

    void ShellController::navigateToLogin() {
        if (m_current_shell != "login") {
            m_current_shell = "login";
            emit currentShellChanged();
        }
    }

    void ShellController::navigateToRegister() {
        if (m_current_shell != "register") {
            m_current_shell = "register";
            emit currentShellChanged();
        }
    }

    void ShellController::navigateToSplash() {
        if (m_current_shell != "splash") {
            m_current_shell = "splash";
            emit currentShellChanged();
        }
    }

    void ShellController::setPageState(const QString& state) {
        if (m_page_state != state) {
            m_page_state = state;
            emit pageStateChanged();
        }
    }

    void ShellController::onOwnerSessionStateChanged() {
        if (!m_session_store) {
            return;
        }

        auto state = m_session_store->GetOwnerManager()->GetState();
        if (state == disk::desktop::OwnerSessionState::LoggedOut ||
            state == disk::desktop::OwnerSessionState::ReauthRequired) {
            navigateToLogin();
        } else if (state == disk::desktop::OwnerSessionState::Active) {
            if (m_current_shell == "login" || m_current_shell == "splash") {
                navigateToOwner();
            }
        }
    }

    void ShellController::onVisitorSessionStateChanged() {
        if (!m_session_store) {
            return;
        }

        auto state = m_session_store->GetVisitorManager()->GetState();
        if (state == disk::desktop::VisitorSessionState::Idle) {
            onOwnerSessionStateChanged();
        }
    }

} // namespace disk::app
