#include <QCoreApplication>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <functional>
#include <memory>

#include <api/AuthApi.hpp>
#include <dtos/AuthDtos.hpp>
#include <services/AuthService.hpp>
#include <services/TokenStore.hpp>
#include <viewmodels/LoginViewModel.hpp>
#include <viewmodels/RegisterViewModel.hpp>

using namespace disk::qml;

// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
class FakeAuthApi : public api::AuthApi {
public:
    FakeAuthApi()
        : api::AuthApi(nullptr) {
    }

    std::function<void(const QString&, const QString&, const QString&, QObject*, api::AuthApiCallback)> registerFn;
    std::function<void(const QString&, const QString&, QObject*, api::AuthApiCallback)> loginFn;
    std::function<void(const QString&, QObject*, api::AuthApiCallback)> refreshFn;
    std::function<void(const QString&, QObject*, api::AuthApiCallback)> logoutFn;

    auto Register(const QString& username, const QString& email, const QString& password, QObject* ctx, api::AuthApiCallback cb) -> void override {
        if (registerFn) {
            registerFn(username, email, password, ctx, std::move(cb));
        }
    }

    auto Login(const QString& account, const QString& password, QObject* ctx, api::AuthApiCallback cb) -> void override {
        if (loginFn) {
            loginFn(account, password, ctx, std::move(cb));
        }
    }

    auto Refresh(const QString& refreshToken, QObject* ctx, api::AuthApiCallback cb) -> void override {
        if (refreshFn) {
            refreshFn(refreshToken, ctx, std::move(cb));
        }
    }

    auto Logout(const QString& accessToken, QObject* ctx, api::AuthApiCallback cb) -> void override {
        if (logoutFn) {
            logoutFn(accessToken, ctx, std::move(cb));
        }
    }
};

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
static auto makeSuccessLoginEnvelope() -> models::ApiEnvelope {
    QJsonObject user;
    user["id"] = 1;
    user["username"] = "testuser";
    user["email"] = "test@example.com";
    user["nickname"] = "";
    user["storage_quota"] = 10737418240.0;
    user["storage_used"] = 0;
    user["created_at"] = "2024-01-01T00:00:00Z";

    QJsonObject data;
    data["access_token"] = "at-123";
    data["refresh_token"] = "rt-456";
    data["token_type"] = "Bearer";
    data["expires_in"] = 7200;
    data["user"] = user;

    models::ApiEnvelope env;
    env.code = 0;
    env.message = "success";
    env.data = data;
    return env;
}

static auto makeSuccessRegisterEnvelope() -> models::ApiEnvelope {
    QJsonObject user;
    user["id"] = 1;
    user["username"] = "testuser";
    user["email"] = "test@example.com";
    user["nickname"] = "";
    user["storage_quota"] = 10737418240.0;
    user["storage_used"] = 0;
    user["created_at"] = "2024-01-01T00:00:00Z";

    QJsonObject data;
    data["user"] = user;

    models::ApiEnvelope env;
    env.code = 0;
    env.message = "success";
    env.data = data;
    return env;
}

// ===========================================================================
class tst_ViewModels : public QObject {
    Q_OBJECT

private:
    FakeAuthApi m_fake_api;
    std::unique_ptr<QTemporaryDir> m_temp_dir;
    std::unique_ptr<services::TokenStore> m_store;
    services::AuthService* m_auth_service = nullptr;

private slots:

    void initTestCase() {
        QCoreApplication::setOrganizationName("DiskTest");
        QCoreApplication::setApplicationName("diskqml-test");
    }

    void init() {
        m_temp_dir = std::make_unique<QTemporaryDir>();
        QVERIFY(m_temp_dir->isValid());
        m_store = std::make_unique<services::TokenStore>(m_temp_dir->path());
        m_fake_api.loginFn = nullptr;
        m_fake_api.registerFn = nullptr;
        m_fake_api.refreshFn = nullptr;
        m_fake_api.logoutFn = nullptr;
        m_auth_service = new services::AuthService(&m_fake_api, m_store.get());
    }

    void cleanup() {
        delete m_auth_service;
        m_auth_service = nullptr;
        m_store.reset();
        m_temp_dir.reset();
    }

    // =======================================================================
    // LoginViewModel
    // =======================================================================

    void loginVm_initialState() {
        viewmodels::LoginViewModel vm(m_auth_service);
        QVERIFY(vm.Account().isEmpty());
        QVERIFY(vm.Password().isEmpty());
        QVERIFY(!vm.Loading());
        QVERIFY(vm.ErrorMessage().isEmpty());
        QVERIFY(!vm.CanSubmit());
    }

    void loginVm_setAccount_emitsSignal() {
        viewmodels::LoginViewModel vm(m_auth_service);
        QSignalSpy spy(&vm, &viewmodels::LoginViewModel::accountChanged);

        vm.SetAccount("user1");
        QCOMPARE(spy.count(), 1);
        QCOMPARE(vm.Account(), QStringLiteral("user1"));

        // Same value → no signal
        vm.SetAccount("user1");
        QCOMPARE(spy.count(), 1);
    }

    void loginVm_setPassword_emitsSignal() {
        viewmodels::LoginViewModel vm(m_auth_service);
        QSignalSpy spy(&vm, &viewmodels::LoginViewModel::passwordChanged);

        vm.SetPassword("pass");
        QCOMPARE(spy.count(), 1);
        QCOMPARE(vm.Password(), QStringLiteral("pass"));
    }

    void loginVm_canSubmit_requiresAccountAndPassword() {
        viewmodels::LoginViewModel vm(m_auth_service);
        QSignalSpy canSubmitSpy(&vm, &viewmodels::LoginViewModel::canSubmitChanged);

        QVERIFY(!vm.CanSubmit());

        vm.SetAccount("user");
        QVERIFY(!vm.CanSubmit());

        vm.SetPassword("pass");
        QVERIFY(vm.CanSubmit());
        QVERIFY(canSubmitSpy.count() > 0);
    }

    void loginVm_canSubmit_falseWhenAccountIsWhitespace() {
        viewmodels::LoginViewModel vm(m_auth_service);
        vm.SetAccount("   ");
        vm.SetPassword("pass");
        QVERIFY(!vm.CanSubmit());
    }

    void loginVm_submit_noOpWhenCannotSubmit() {
        viewmodels::LoginViewModel vm(m_auth_service);
        QSignalSpy loadingSpy(&vm, &viewmodels::LoginViewModel::loadingChanged);

        vm.submit(); // account & password empty
        QCOMPARE(loadingSpy.count(), 0);
    }

    void loginVm_submit_success_emitsLoginSucceeded() {
        m_fake_api.loginFn = [](auto, auto, auto, api::AuthApiCallback cb) {
            cb(makeSuccessLoginEnvelope(), QString{});
        };

        viewmodels::LoginViewModel vm(m_auth_service);
        QSignalSpy successSpy(&vm, &viewmodels::LoginViewModel::loginSucceeded);
        QSignalSpy loadingSpy(&vm, &viewmodels::LoginViewModel::loadingChanged);
        QSignalSpy errorSpy(&vm, &viewmodels::LoginViewModel::errorMessageChanged);

        vm.SetAccount("testuser");
        vm.SetPassword("Password1");
        vm.submit();

        QCOMPARE(successSpy.count(), 1);
        QCOMPARE(successSpy.at(0).at(0).toString(), QStringLiteral("testuser"));
        // Loading should have toggled: true → false
        QVERIFY(loadingSpy.count() >= 2);
        QVERIFY(!vm.Loading());
        QVERIFY(vm.ErrorMessage().isEmpty());
    }

    void loginVm_submit_error_setsErrorMessage() {
        m_fake_api.loginFn = [](auto, auto, auto, api::AuthApiCallback cb) {
            models::ApiEnvelope env;
            env.code = 40101;
            env.message = "invalid credentials";
            cb(env, QString{});
        };

        viewmodels::LoginViewModel vm(m_auth_service);
        QSignalSpy successSpy(&vm, &viewmodels::LoginViewModel::loginSucceeded);
        QSignalSpy errorSpy(&vm, &viewmodels::LoginViewModel::errorMessageChanged);

        vm.SetAccount("testuser");
        vm.SetPassword("Password1");
        vm.submit();

        QCOMPARE(successSpy.count(), 0);
        QVERIFY(!vm.ErrorMessage().isEmpty());
        QVERIFY(errorSpy.count() > 0);
    }

    void loginVm_clearError() {
        m_fake_api.loginFn = [](auto, auto, auto, api::AuthApiCallback cb) {
            models::ApiEnvelope env;
            env.code = 40101;
            env.message = "fail";
            cb(env, QString{});
        };

        viewmodels::LoginViewModel vm(m_auth_service);
        vm.SetAccount("testuser");
        vm.SetPassword("Password1");
        vm.submit();
        QVERIFY(!vm.ErrorMessage().isEmpty());

        vm.clearError();
        QVERIFY(vm.ErrorMessage().isEmpty());
    }

    // =======================================================================
    // RegisterViewModel
    // =======================================================================

    void registerVm_initialState() {
        viewmodels::RegisterViewModel vm(m_auth_service);
        QVERIFY(vm.Username().isEmpty());
        QVERIFY(vm.Email().isEmpty());
        QVERIFY(vm.Password().isEmpty());
        QVERIFY(vm.ConfirmPassword().isEmpty());
        QVERIFY(!vm.Loading());
        QVERIFY(vm.ErrorMessage().isEmpty());
        QVERIFY(!vm.CanSubmit());
        QVERIFY(vm.UsernameError().isEmpty());
        QVERIFY(vm.EmailError().isEmpty());
        QVERIFY(vm.PasswordError().isEmpty());
        QVERIFY(vm.ConfirmPasswordError().isEmpty());
    }

    void registerVm_fieldValidation_username() {
        viewmodels::RegisterViewModel vm(m_auth_service);
        QSignalSpy usernameErrSpy(&vm, &viewmodels::RegisterViewModel::usernameErrorChanged);

        // Invalid username → shows error
        vm.SetUsername("ab");
        QVERIFY(!vm.UsernameError().isEmpty());
        QVERIFY(usernameErrSpy.count() > 0);

        // Valid username → clears error
        vm.SetUsername("validuser");
        QVERIFY(vm.UsernameError().isEmpty());
    }

    void registerVm_fieldValidation_email() {
        viewmodels::RegisterViewModel vm(m_auth_service);
        QSignalSpy emailErrSpy(&vm, &viewmodels::RegisterViewModel::emailErrorChanged);

        vm.SetEmail("notanemail");
        QVERIFY(!vm.EmailError().isEmpty());
        QVERIFY(emailErrSpy.count() > 0);

        vm.SetEmail("valid@example.com");
        QVERIFY(vm.EmailError().isEmpty());
    }

    void registerVm_fieldValidation_password() {
        viewmodels::RegisterViewModel vm(m_auth_service);
        QSignalSpy pwErrSpy(&vm, &viewmodels::RegisterViewModel::passwordErrorChanged);

        vm.SetPassword("weak");
        QVERIFY(!vm.PasswordError().isEmpty());
        QVERIFY(pwErrSpy.count() > 0);

        vm.SetPassword("StrongPass1");
        QVERIFY(vm.PasswordError().isEmpty());
    }

    void registerVm_fieldValidation_confirmPassword() {
        viewmodels::RegisterViewModel vm(m_auth_service);
        QSignalSpy confirmErrSpy(&vm, &viewmodels::RegisterViewModel::confirmPasswordErrorChanged);

        vm.SetPassword("StrongPass1");
        vm.SetConfirmPassword("Different1");
        QVERIFY(!vm.ConfirmPasswordError().isEmpty());
        QVERIFY(confirmErrSpy.count() > 0);

        vm.SetConfirmPassword("StrongPass1");
        QVERIFY(vm.ConfirmPasswordError().isEmpty());
    }

    void registerVm_canSubmit_allFieldsValidAndFilled() {
        viewmodels::RegisterViewModel vm(m_auth_service);
        QSignalSpy canSubmitSpy(&vm, &viewmodels::RegisterViewModel::canSubmitChanged);

        QVERIFY(!vm.CanSubmit());

        vm.SetUsername("testuser");
        vm.SetEmail("test@example.com");
        vm.SetPassword("StrongPass1");
        vm.SetConfirmPassword("StrongPass1");

        QVERIFY(vm.CanSubmit());
        QVERIFY(canSubmitSpy.count() > 0);
    }

    void registerVm_canSubmit_falseWhenValidationErrors() {
        viewmodels::RegisterViewModel vm(m_auth_service);

        vm.SetUsername("ab"); // too short
        vm.SetEmail("test@example.com");
        vm.SetPassword("StrongPass1");
        vm.SetConfirmPassword("StrongPass1");

        QVERIFY(!vm.CanSubmit()); // username error present
    }

    void registerVm_canSubmit_falseWhenPasswordMismatch() {
        viewmodels::RegisterViewModel vm(m_auth_service);

        vm.SetUsername("testuser");
        vm.SetEmail("test@example.com");
        vm.SetPassword("StrongPass1");
        vm.SetConfirmPassword("StrongPass2");

        QVERIFY(!vm.CanSubmit());
    }

    void registerVm_submit_noOpWhenCannotSubmit() {
        viewmodels::RegisterViewModel vm(m_auth_service);
        QSignalSpy loadingSpy(&vm, &viewmodels::RegisterViewModel::loadingChanged);

        vm.submit();
        QCOMPARE(loadingSpy.count(), 0);
    }

    void registerVm_submit_success_emitsRegisterSucceeded() {
        m_fake_api.registerFn = [](auto, auto, auto, auto, api::AuthApiCallback cb) {
            cb(makeSuccessRegisterEnvelope(), QString{});
        };

        viewmodels::RegisterViewModel vm(m_auth_service);
        QSignalSpy successSpy(&vm, &viewmodels::RegisterViewModel::registerSucceeded);
        QSignalSpy loadingSpy(&vm, &viewmodels::RegisterViewModel::loadingChanged);

        vm.SetUsername("testuser");
        vm.SetEmail("test@example.com");
        vm.SetPassword("StrongPass1");
        vm.SetConfirmPassword("StrongPass1");
        vm.submit();

        QCOMPARE(successSpy.count(), 1);
        QCOMPARE(successSpy.at(0).at(0).toString(), QStringLiteral("testuser"));
        QCOMPARE(successSpy.at(0).at(1).toString(), QStringLiteral("test@example.com"));
        QVERIFY(loadingSpy.count() >= 2);
        QVERIFY(!vm.Loading());
    }

    void registerVm_submit_error_setsErrorMessage() {
        m_fake_api.registerFn = [](auto, auto, auto, auto, api::AuthApiCallback cb) {
            models::ApiEnvelope env;
            env.code = 40001;
            env.message = "username exists";
            cb(env, QString{});
        };

        viewmodels::RegisterViewModel vm(m_auth_service);
        QSignalSpy successSpy(&vm, &viewmodels::RegisterViewModel::registerSucceeded);
        QSignalSpy errorSpy(&vm, &viewmodels::RegisterViewModel::errorMessageChanged);

        vm.SetUsername("testuser");
        vm.SetEmail("test@example.com");
        vm.SetPassword("StrongPass1");
        vm.SetConfirmPassword("StrongPass1");
        vm.submit();

        QCOMPARE(successSpy.count(), 0);
        QVERIFY(!vm.ErrorMessage().isEmpty());
        QVERIFY(errorSpy.count() > 0);
    }

    void registerVm_clearError_clearsAllErrors() {
        viewmodels::RegisterViewModel vm(m_auth_service);

        // Create field errors
        vm.SetUsername("ab");
        vm.SetEmail("bad");
        vm.SetPassword("weak");
        vm.SetConfirmPassword("diff");

        QVERIFY(!vm.UsernameError().isEmpty());
        QVERIFY(!vm.EmailError().isEmpty());
        QVERIFY(!vm.PasswordError().isEmpty());
        QVERIFY(!vm.ConfirmPasswordError().isEmpty());

        vm.clearError();

        QVERIFY(vm.UsernameError().isEmpty());
        QVERIFY(vm.EmailError().isEmpty());
        QVERIFY(vm.PasswordError().isEmpty());
        QVERIFY(vm.ConfirmPasswordError().isEmpty());
        QVERIFY(vm.ErrorMessage().isEmpty());
    }

    void registerVm_setPassword_triggersConfirmRevalidation() {
        viewmodels::RegisterViewModel vm(m_auth_service);

        vm.SetPassword("StrongPass1");
        vm.SetConfirmPassword("StrongPass1");
        QVERIFY(vm.ConfirmPasswordError().isEmpty());

        // Changing password should re-validate confirm password
        vm.SetPassword("StrongPass2");
        QVERIFY(!vm.ConfirmPasswordError().isEmpty());
    }

    void registerVm_duplicateSetValue_noSignal() {
        viewmodels::RegisterViewModel vm(m_auth_service);
        vm.SetUsername("user1");
        QSignalSpy spy(&vm, &viewmodels::RegisterViewModel::usernameChanged);
        vm.SetUsername("user1"); // same value
        QCOMPARE(spy.count(), 0);
    }
};

QTEST_MAIN(tst_ViewModels)
#include "tst_ViewModels.moc"
