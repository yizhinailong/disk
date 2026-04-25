#pragma once

#include <QObject>
#include <QString>

namespace disk::desktop {
    class SessionStore;
}

namespace disk::app {

    class ShellController : public QObject {
        Q_OBJECT
        Q_PROPERTY(QString currentShell READ GetCurrentShell NOTIFY currentShellChanged)
        Q_PROPERTY(QString pageState READ GetPageState NOTIFY pageStateChanged)

    public:
        explicit ShellController(disk::desktop::SessionStore* session_store, QObject* parent = nullptr);
        ~ShellController() override = default;

        void Initialize();

        QString GetCurrentShell() const;
        QString GetPageState() const;

    public slots:
        void navigateToOwner();
        void navigateToVisitor(const QString& shareId);
        void navigateToLogin();
        void navigateToRegister();
        void navigateToSplash();

        void setPageState(const QString& state);

    signals:
        void currentShellChanged();
        void pageStateChanged();

    private slots:
        void onOwnerSessionStateChanged();
        void onVisitorSessionStateChanged();

    private:
        disk::desktop::SessionStore* m_session_store;
        QString m_current_shell;
        QString m_page_state;
    };

} // namespace disk::app
