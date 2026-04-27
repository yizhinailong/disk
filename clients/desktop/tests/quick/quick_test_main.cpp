#include <QtQuickTest/quicktest.h>
#include <QQmlEngine>
#include <QQmlContext>

// ── Minimal stub objects matching the auth context property API ────────────
// These are injected as QML context properties so that runtime-instantiated
// auth pages (LoginPage, RegisterPage, AuthShell) do not produce
// "ReferenceError: authService is not defined" warnings.

class StubAuthService : public QObject {
    Q_OBJECT
public:
    explicit StubAuthService(QObject *parent = nullptr) : QObject(parent) {}
    Q_INVOKABLE void Login(const QString &, const QString &) {}
    Q_INVOKABLE void Register(const QString &, const QString &, const QString &) {}
signals:
    void loginSuccess(const QVariant &, const QVariant &, const QVariant &, const QVariant &);
    void loginFailure(const QVariant &, const QVariant &);
    void registerSuccess(const QVariant &);
    void registerFailure(const QVariant &, const QVariant &);
};

class StubShellController : public QObject {
    Q_OBJECT
public:
    explicit StubShellController(QObject *parent = nullptr) : QObject(parent) {}
    Q_INVOKABLE void navigateToLogin() {}
    Q_INVOKABLE void navigateToRegister() {}
};

class StubOwnerSession : public QObject {
    Q_OBJECT
public:
    explicit StubOwnerSession(QObject *parent = nullptr) : QObject(parent) {}
    Q_INVOKABLE void StartLogin() {}
};

class StubSessionStore : public QObject {
    Q_OBJECT
    Q_PROPERTY(StubOwnerSession *owner READ owner CONSTANT)
public:
    explicit StubSessionStore(QObject *parent = nullptr)
        : QObject(parent), m_owner(new StubOwnerSession(this)) {}
    StubOwnerSession *owner() const { return m_owner; }
private:
    StubOwnerSession *m_owner;
};

// ── Setup object passed to quick_test_main_with_setup ──────────────────────
// Qt exposes this as a context property so QML can call inject() during
// initTestCase(), setting authService/shellController/sessionStore on the
// root context *before* any page component is instantiated.

class QuickTestSetup : public QObject {
    Q_OBJECT
public:
    explicit QuickTestSetup(QObject *parent = nullptr) : QObject(parent) {
        m_authService = new StubAuthService(this);
        m_shellController = new StubShellController(this);
        m_sessionStore = new StubSessionStore(this);
        // Defer injection until the event loop processes events.
        // By then quick_test_main_with_setup has created the QQmlEngine
        // and assigned this object's QQmlContext, so setContextProperty works.
        QMetaObject::invokeMethod(this, &QuickTestSetup::inject,
                                  Qt::QueuedConnection);
    }

    Q_INVOKABLE void inject() {
        auto ctx = QQmlEngine::contextForObject(this);
        if (ctx) {
            ctx->setContextProperty("authService", m_authService);
            ctx->setContextProperty("shellController", m_shellController);
            ctx->setContextProperty("sessionStore", m_sessionStore);
        }
    }

private:
    StubAuthService *m_authService;
    StubShellController *m_shellController;
    StubSessionStore *m_sessionStore;
};

int main(int argc, char **argv) {
    QTEST_SET_MAIN_SOURCE_PATH
    static QuickTestSetup setup;
    return quick_test_main_with_setup(argc, argv, "desktop-quick-tests",
                                      QUICK_TEST_SOURCE_DIR, &setup);
}

#include "quick_test_main.moc"
