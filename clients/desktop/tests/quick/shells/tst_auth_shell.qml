import QtQuick 2.15
import QtTest 1.15

TestCase {
    name: "DesktopAuthShell"
    id: testAuthShell

    // ── Auth-entry objectName hook contract ────────────────────────────────
    // These objectName values are the stable test hooks that the future shared
    // auth shell (task 3) MUST preserve.  They let integration tests find
    // controls by name regardless of whether the page lives inside a dedicated
    // window or a unified auth shell.

    function test_login_page_root_has_auth_objectName() {
        var xhr = new XMLHttpRequest()
        xhr.open("GET", Qt.resolvedUrl("../../../qml/pages/LoginPage.qml"), false)
        xhr.send()
        var source = xhr.responseText
        verify(source.length > 0, "LoginPage.qml was read")
        verify(source.indexOf('objectName: "authLoginPage"') !== -1,
               "Login page root has authLoginPage objectName hook")
    }

    function test_register_page_root_has_auth_objectName() {
        var xhr = new XMLHttpRequest()
        xhr.open("GET", Qt.resolvedUrl("../../../qml/pages/RegisterPage.qml"), false)
        xhr.send()
        var source = xhr.responseText
        verify(source.length > 0, "RegisterPage.qml was read")
        verify(source.indexOf('objectName: "authRegisterPage"') !== -1,
               "Register page root has authRegisterPage objectName hook")
    }

    function test_login_page_controls_have_stable_objectNames() {
        var xhr = new XMLHttpRequest()
        xhr.open("GET", Qt.resolvedUrl("../../../qml/pages/LoginPage.qml"), false)
        xhr.send()
        var source = xhr.responseText
        verify(source.length > 0, "LoginPage.qml was read")
        verify(source.indexOf('objectName: "authUsernameField"') !== -1,
               "Login username field has authUsernameField objectName")
        verify(source.indexOf('objectName: "authPasswordField"') !== -1,
               "Login password field has authPasswordField objectName")
        verify(source.indexOf('objectName: "authSubmitButton"') !== -1,
               "Login submit button has authSubmitButton objectName")
        verify(source.indexOf('objectName: "authModeSwitchCta"') !== -1,
               "Login mode-switch CTA has authModeSwitchCta objectName")
        verify(source.indexOf('objectName: "authErrorLabel"') !== -1,
               "Login error label has authErrorLabel objectName")
    }

    function test_register_page_controls_have_stable_objectNames() {
        var xhr = new XMLHttpRequest()
        xhr.open("GET", Qt.resolvedUrl("../../../qml/pages/RegisterPage.qml"), false)
        xhr.send()
        var source = xhr.responseText
        verify(source.length > 0, "RegisterPage.qml was read")
        verify(source.indexOf('objectName: "authUsernameField"') !== -1,
               "Register username field has authUsernameField objectName")
        verify(source.indexOf('objectName: "authEmailField"') !== -1,
               "Register email field has authEmailField objectName")
        verify(source.indexOf('objectName: "authPasswordField"') !== -1,
               "Register password field has authPasswordField objectName")
        verify(source.indexOf('objectName: "authConfirmPasswordField"') !== -1,
               "Register confirm-password field has authConfirmPasswordField objectName")
        verify(source.indexOf('objectName: "authSubmitButton"') !== -1,
               "Register submit button has authSubmitButton objectName")
        verify(source.indexOf('objectName: "authModeSwitchCta"') !== -1,
               "Register mode-switch CTA has authModeSwitchCta objectName")
        verify(source.indexOf('objectName: "authErrorLabel"') !== -1,
               "Register error label has authErrorLabel objectName")
    }

    function test_register_page_success_timer_has_objectName() {
        var xhr = new XMLHttpRequest()
        xhr.open("GET", Qt.resolvedUrl("../../../qml/pages/RegisterPage.qml"), false)
        xhr.send()
        var source = xhr.responseText
        verify(source.length > 0, "RegisterPage.qml was read")
        verify(source.indexOf('objectName: "authRegisterSuccessTimer"') !== -1,
               "Register success timer has authRegisterSuccessTimer objectName")
    }

    // ── Shared objectName naming contract ──────────────────────────────────
    // Login and register pages MUST use the SAME objectName for controls that
    // represent the same semantic element (e.g., both use "authUsernameField").
    // This allows the future shared auth shell to reuse a single set of hooks.

    function test_shared_field_objectNames_are_consistent_across_auth_pages() {
        var loginXhr = new XMLHttpRequest()
        loginXhr.open("GET", Qt.resolvedUrl("../../../qml/pages/LoginPage.qml"), false)
        loginXhr.send()
        var loginSource = loginXhr.responseText

        var registerXhr = new XMLHttpRequest()
        registerXhr.open("GET", Qt.resolvedUrl("../../../qml/pages/RegisterPage.qml"), false)
        registerXhr.send()
        var registerSource = registerXhr.responseText

        verify(loginSource.length > 0, "LoginPage.qml was read")
        verify(registerSource.length > 0, "RegisterPage.qml was read")

        // Both pages use the same objectName for the same semantic control
        verify(loginSource.indexOf('objectName: "authUsernameField"') !== -1 &&
               registerSource.indexOf('objectName: "authUsernameField"') !== -1,
               "Both pages share authUsernameField objectName")
        verify(loginSource.indexOf('objectName: "authPasswordField"') !== -1 &&
               registerSource.indexOf('objectName: "authPasswordField"') !== -1,
               "Both pages share authPasswordField objectName")
        verify(loginSource.indexOf('objectName: "authSubmitButton"') !== -1 &&
               registerSource.indexOf('objectName: "authSubmitButton"') !== -1,
               "Both pages share authSubmitButton objectName")
        verify(loginSource.indexOf('objectName: "authModeSwitchCta"') !== -1 &&
               registerSource.indexOf('objectName: "authModeSwitchCta"') !== -1,
               "Both pages share authModeSwitchCta objectName")
        verify(loginSource.indexOf('objectName: "authErrorLabel"') !== -1 &&
               registerSource.indexOf('objectName: "authErrorLabel"') !== -1,
               "Both pages share authErrorLabel objectName")
    }

    // ── ShellController auth navigation contract ───────────────────────────

    function test_shell_controller_has_auth_navigation_methods() {
        var xhr = new XMLHttpRequest()
        xhr.open("GET", Qt.resolvedUrl("../../../src/app/ShellController.hpp"), false)
        xhr.send()
        var source = xhr.responseText

        verify(source.length > 0, "ShellController.hpp was read")
        verify(source.indexOf("navigateToLogin") !== -1,
               "Has navigateToLogin method")
        verify(source.indexOf("navigateToRegister") !== -1,
               "Has navigateToRegister method")
    }

    // ── Auth service integration contract ──────────────────────────────────

    function test_login_page_uses_approved_auth_service_calls() {
        var xhr = new XMLHttpRequest()
        xhr.open("GET", Qt.resolvedUrl("../../../qml/pages/LoginPage.qml"), false)
        xhr.send()
        var source = xhr.responseText

        verify(source.length > 0, "LoginPage.qml was read")
        verify(source.indexOf("sessionStore.owner.StartLogin()") !== -1,
               "Login triggers owner session StartLogin flow")
        verify(source.indexOf("authService.Login(") !== -1,
               "Login calls authService.Login")
        verify(source.indexOf("onLoginSuccess") !== -1,
               "Login handles onLoginSuccess signal")
        verify(source.indexOf("onLoginFailure") !== -1,
               "Login handles onLoginFailure signal")
    }

    function test_register_page_uses_approved_auth_service_calls() {
        var xhr = new XMLHttpRequest()
        xhr.open("GET", Qt.resolvedUrl("../../../qml/pages/RegisterPage.qml"), false)
        xhr.send()
        var source = xhr.responseText

        verify(source.length > 0, "RegisterPage.qml was read")
        verify(source.indexOf("authService.Register(") !== -1,
               "Register calls authService.Register")
        verify(source.indexOf("onRegisterSuccess") !== -1,
               "Register handles onRegisterSuccess signal")
        verify(source.indexOf("onRegisterFailure") !== -1,
               "Register handles onRegisterFailure signal")
        verify(source.indexOf("returnToLoginTimer.start()") !== -1,
               "Register starts success timer on register success")
    }

    // ── Absence of excluded auth affordances across all auth pages ─────────

    function test_auth_pages_have_no_third_party_login() {
        var loginXhr = new XMLHttpRequest()
        loginXhr.open("GET", Qt.resolvedUrl("../../../qml/pages/LoginPage.qml"), false)
        loginXhr.send()
        var loginSource = loginXhr.responseText

        var registerXhr = new XMLHttpRequest()
        registerXhr.open("GET", Qt.resolvedUrl("../../../qml/pages/RegisterPage.qml"), false)
        registerXhr.send()
        var registerSource = registerXhr.responseText

        verify(loginSource.length > 0, "LoginPage.qml was read")
        verify(registerSource.length > 0, "RegisterPage.qml was read")

        var excluded = ["WeChat", "QQ", "Weibo", "Google", "GitHub", "Baidu", "Apple", "Facebook", "Microsoft", "OAuth"]
        for (var i = 0; i < excluded.length; i++) {
            verify(loginSource.indexOf(excluded[i]) === -1,
                   "LoginPage has no " + excluded[i] + " login")
            verify(registerSource.indexOf(excluded[i]) === -1,
                   "RegisterPage has no " + excluded[i] + " login")
        }
    }

    function test_auth_pages_have_no_qr_phone_sms_captcha() {
        var loginXhr = new XMLHttpRequest()
        loginXhr.open("GET", Qt.resolvedUrl("../../../qml/pages/LoginPage.qml"), false)
        loginXhr.send()
        var loginSource = loginXhr.responseText

        var registerXhr = new XMLHttpRequest()
        registerXhr.open("GET", Qt.resolvedUrl("../../../qml/pages/RegisterPage.qml"), false)
        registerXhr.send()
        var registerSource = registerXhr.responseText

        verify(loginSource.length > 0, "LoginPage.qml was read")
        verify(registerSource.length > 0, "RegisterPage.qml was read")

        var excluded = ["QRCode", "Scan", "SMS", "captcha", "Captcha", "slider", "puzzle"]
        for (var i = 0; i < excluded.length; i++) {
            verify(loginSource.indexOf(excluded[i]) === -1,
                   "LoginPage has no " + excluded[i])
            verify(registerSource.indexOf(excluded[i]) === -1,
                   "RegisterPage has no " + excluded[i])
        }
        verify(loginSource.indexOf("phone") === -1, "LoginPage has no phone field")
        verify(registerSource.indexOf("phone") === -1, "RegisterPage has no phone field")
    }

    function test_auth_pages_have_no_marketing_or_feature_cards() {
        var loginXhr = new XMLHttpRequest()
        loginXhr.open("GET", Qt.resolvedUrl("../../../qml/pages/LoginPage.qml"), false)
        loginXhr.send()
        var loginSource = loginXhr.responseText

        var registerXhr = new XMLHttpRequest()
        registerXhr.open("GET", Qt.resolvedUrl("../../../qml/pages/RegisterPage.qml"), false)
        registerXhr.send()
        var registerSource = registerXhr.responseText

        verify(loginSource.length > 0, "LoginPage.qml was read")
        verify(registerSource.length > 0, "RegisterPage.qml was read")

        var excluded = ["VIP", "Premium", "free trial", "upgrade", "discount"]
        for (var i = 0; i < excluded.length; i++) {
            verify(loginSource.indexOf(excluded[i]) === -1,
                   "LoginPage has no " + excluded[i])
            verify(registerSource.indexOf(excluded[i]) === -1,
                   "RegisterPage has no " + excluded[i])
        }
        verify(loginSource.indexOf("sync") === -1, "LoginPage has no sync promo")
        verify(loginSource.indexOf("backup") === -1, "LoginPage has no backup promo")
        verify(registerSource.indexOf("sync") === -1, "RegisterPage has no sync promo")
        verify(registerSource.indexOf("backup") === -1, "RegisterPage has no backup promo")
    }

    // ── Shared auth shell routing contract (task 3) ─────────────────────────
    // Main.qml must route both "login" and "register" shell states to a single
    // authShellComponent backed by AuthShell.qml, which uses an authMode property
    // and a Loader to swap pages without destroying/recreating the window.

    function test_main_qml_routes_login_to_authShellComponent() {
        var xhr = new XMLHttpRequest()
        xhr.open("GET", Qt.resolvedUrl("../../../qml/Main.qml"), false)
        xhr.send()
        var source = xhr.responseText

        verify(source.length > 0, "Main.qml was read")
        verify(source.indexOf('case "login": return authShellComponent') !== -1,
               "login shell state maps to authShellComponent")
    }

    function test_main_qml_routes_register_to_authShellComponent() {
        var xhr = new XMLHttpRequest()
        xhr.open("GET", Qt.resolvedUrl("../../../qml/Main.qml"), false)
        xhr.send()
        var source = xhr.responseText

        verify(source.length > 0, "Main.qml was read")
        verify(source.indexOf('case "register": return authShellComponent') !== -1,
               "register shell state maps to authShellComponent")
    }

    function test_main_qml_defines_authShellComponent() {
        var xhr = new XMLHttpRequest()
        xhr.open("GET", Qt.resolvedUrl("../../../qml/Main.qml"), false)
        xhr.send()
        var source = xhr.responseText

        verify(source.length > 0, "Main.qml was read")
        verify(source.indexOf('id: authShellComponent') !== -1,
               "Main.qml defines authShellComponent")
    }

    function test_main_qml_presentShell_reuses_auth_window_on_auth_to_auth_transition() {
        var xhr = new XMLHttpRequest()
        xhr.open("GET", Qt.resolvedUrl("../../../qml/Main.qml"), false)
        xhr.send()
        var source = xhr.responseText

        verify(source.length > 0, "Main.qml was read")
        verify(source.indexOf('currentWindow.authMode !== undefined') !== -1,
               "presentShell detects existing auth window via authMode property")
        verify(source.indexOf('currentWindow.authMode = nextShell') !== -1,
               "presentShell reuses auth window by switching authMode instead of recreating")
    }

    function test_authShell_qml_has_authMode_property() {
        var xhr = new XMLHttpRequest()
        xhr.open("GET", Qt.resolvedUrl("../../../qml/shells/AuthShell.qml"), false)
        xhr.send()
        var source = xhr.responseText

        verify(source.length > 0, "AuthShell.qml was read")
        verify(source.indexOf('property string authMode: "login"') !== -1,
               "AuthShell declares authMode property with login default")
    }

    function test_authShell_qml_uses_loader_with_authMode_switching() {
        var xhr = new XMLHttpRequest()
        xhr.open("GET", Qt.resolvedUrl("../../../qml/shells/AuthShell.qml"), false)
        xhr.send()
        var source = xhr.responseText

        verify(source.length > 0, "AuthShell.qml was read")
        verify(source.indexOf('sourceComponent: root.authMode === "login" ? loginPageComponent : registerPageComponent') !== -1,
               "AuthShell Loader switches between login and register based on authMode")
    }

    function test_authShell_qml_hosts_login_and_register_pages() {
        var xhr = new XMLHttpRequest()
        xhr.open("GET", Qt.resolvedUrl("../../../qml/shells/AuthShell.qml"), false)
        xhr.send()
        var source = xhr.responseText

        verify(source.length > 0, "AuthShell.qml was read")
        verify(source.indexOf("LoginPage") !== -1,
               "AuthShell references LoginPage")
        verify(source.indexOf("RegisterPage") !== -1,
               "AuthShell references RegisterPage")
    }

    function test_authShell_qml_preserves_entry_size() {
        var xhr = new XMLHttpRequest()
        xhr.open("GET", Qt.resolvedUrl("../../../qml/shells/AuthShell.qml"), false)
        xhr.send()
        var source = xhr.responseText

        verify(source.length > 0, "AuthShell.qml was read")
        verify(source.indexOf("width: 1024") !== -1,
               "AuthShell uses 1024 width matching desktop entry size")
        verify(source.indexOf("height: 768") !== -1,
               "AuthShell uses 768 height matching desktop entry size")
    }

    // ── Busy-state guard contract (task 6) ────────────────────────────────────

    function test_authShell_qml_has_busy_property() {
        var xhr = new XMLHttpRequest()
        xhr.open("GET", Qt.resolvedUrl("../../../qml/shells/AuthShell.qml"), false)
        xhr.send()
        var source = xhr.responseText

        verify(source.length > 0, "AuthShell.qml was read")
        verify(source.indexOf("readonly property bool busy:") !== -1,
               "AuthShell declares a readonly busy property")
        verify(source.indexOf("pageLoader.item") !== -1 &&
               source.indexOf("isBusy") !== -1,
               "AuthShell busy property reads from loaded page's isBusy")
    }

    function test_authShell_qml_guards_mode_switch_when_busy() {
        var xhr = new XMLHttpRequest()
        xhr.open("GET", Qt.resolvedUrl("../../../qml/shells/AuthShell.qml"), false)
        xhr.send()
        var source = xhr.responseText

        verify(source.length > 0, "AuthShell.qml was read")
        verify(source.indexOf("onAuthModeChanged:") !== -1,
               "AuthShell has onAuthModeChanged handler")
        verify(source.indexOf("root.busy") !== -1,
               "AuthShell onAuthModeChanged checks busy state")
    }

    function test_authShell_qml_title_syncs_with_authMode() {
        var xhr = new XMLHttpRequest()
        xhr.open("GET", Qt.resolvedUrl("../../../qml/shells/AuthShell.qml"), false)
        xhr.send()
        var source = xhr.responseText

        verify(source.length > 0, "AuthShell.qml was read")
        verify(source.indexOf('root.authMode === "login" ? "Disk Desktop - Login" : "Disk Desktop - Register"') !== -1,
               "AuthShell title binding reflects current authMode")
    }

    function test_authShell_qml_hero_text_syncs_with_authMode() {
        var xhr = new XMLHttpRequest()
        xhr.open("GET", Qt.resolvedUrl("../../../qml/shells/AuthShell.qml"), false)
        xhr.send()
        var source = xhr.responseText

        verify(source.length > 0, "AuthShell.qml was read")
        verify(source.indexOf('root.authMode === "login" ? "Calm entry') !== -1,
               "AuthShell hero heading text reflects login mode")
        verify(source.indexOf('"A quieter way to start') !== -1,
               "AuthShell hero heading text reflects register mode")
    }

    // ── Cross-mode reset contract (task 6) ────────────────────────────────────

    function test_login_page_has_resetState_function() {
        var xhr = new XMLHttpRequest()
        xhr.open("GET", Qt.resolvedUrl("../../../qml/pages/LoginPage.qml"), false)
        xhr.send()
        var source = xhr.responseText

        verify(source.length > 0, "LoginPage.qml was read")
        verify(source.indexOf("function resetState()") !== -1,
               "LoginPage declares a resetState function")
        verify(source.indexOf("usernameField.text = \"\"") !== -1,
               "resetState clears username field")
        verify(source.indexOf("passwordField.text = \"\"") !== -1,
               "resetState clears password field")
        verify(source.indexOf("errorLabel.text = \"\"") !== -1,
               "resetState clears error label")
        verify(source.indexOf("root.isBusy = false") !== -1,
               "resetState resets isBusy to false")
        verify(source.indexOf("usernameField.forceActiveFocus()") !== -1,
               "resetState focuses username field on mode entry")
    }

    function test_login_page_calls_resetState_on_completed() {
        var xhr = new XMLHttpRequest()
        xhr.open("GET", Qt.resolvedUrl("../../../qml/pages/LoginPage.qml"), false)
        xhr.send()
        var source = xhr.responseText

        verify(source.length > 0, "LoginPage.qml was read")
        verify(source.indexOf("Component.onCompleted: resetState()") !== -1,
               "LoginPage calls resetState on Component.onCompleted")
    }

    function test_register_page_has_resetState_function() {
        var xhr = new XMLHttpRequest()
        xhr.open("GET", Qt.resolvedUrl("../../../qml/pages/RegisterPage.qml"), false)
        xhr.send()
        var source = xhr.responseText

        verify(source.length > 0, "RegisterPage.qml was read")
        verify(source.indexOf("function resetState()") !== -1,
               "RegisterPage declares a resetState function")
        verify(source.indexOf("usernameField.text = \"\"") !== -1,
               "resetState clears username field")
        verify(source.indexOf("emailField.text = \"\"") !== -1,
               "resetState clears email field")
        verify(source.indexOf("passwordField.text = \"\"") !== -1,
               "resetState clears password field")
        verify(source.indexOf("confirmPasswordField.text = \"\"") !== -1,
               "resetState clears confirm password field")
        verify(source.indexOf("messageLabel.text = \"\"") !== -1,
               "resetState clears message label")
        verify(source.indexOf("root.isBusy = false") !== -1,
               "resetState resets isBusy to false")
        verify(source.indexOf("returnToLoginTimer.stop()") !== -1,
               "resetState stops return-to-login timer")
        verify(source.indexOf("usernameField.forceActiveFocus()") !== -1,
               "resetState focuses username field on mode entry")
    }

    function test_register_page_calls_resetState_on_completed() {
        var xhr = new XMLHttpRequest()
        xhr.open("GET", Qt.resolvedUrl("../../../qml/pages/RegisterPage.qml"), false)
        xhr.send()
        var source = xhr.responseText

        verify(source.length > 0, "RegisterPage.qml was read")
        verify(source.indexOf("Component.onCompleted: resetState()") !== -1,
               "RegisterPage calls resetState on Component.onCompleted")
    }

    // ── Mode-switch guard contract ────────────────────────────────────────────
    // Both login and register mode-switch CTAs must be disabled when isBusy.

    function test_login_mode_switch_cta_disabled_when_busy() {
        var xhr = new XMLHttpRequest()
        xhr.open("GET", Qt.resolvedUrl("../../../qml/pages/LoginPage.qml"), false)
        xhr.send()
        var source = xhr.responseText

        verify(source.length > 0, "LoginPage.qml was read")
        verify(source.indexOf('objectName: "authModeSwitchCta"') !== -1,
               "Login mode-switch CTA has authModeSwitchCta objectName")
        verify(source.indexOf("enabled: !root.isBusy") !== -1,
               "Login mode-switch CTA is disabled during busy state")
    }

    function test_register_mode_switch_cta_disabled_when_busy() {
        var xhr = new XMLHttpRequest()
        xhr.open("GET", Qt.resolvedUrl("../../../qml/pages/RegisterPage.qml"), false)
        xhr.send()
        var source = xhr.responseText

        verify(source.length > 0, "RegisterPage.qml was read")
        verify(source.indexOf('objectName: "authModeSwitchCta"') !== -1,
               "Register mode-switch CTA has authModeSwitchCta objectName")
        verify(source.indexOf("enabled: !root.isBusy") !== -1,
               "Register mode-switch CTA is disabled during busy state")
    }

    // ── Submit button double-submit guard ──────────────────────────────────────

    function test_login_submit_button_disabled_when_busy() {
        var xhr = new XMLHttpRequest()
        xhr.open("GET", Qt.resolvedUrl("../../../qml/pages/LoginPage.qml"), false)
        xhr.send()
        var source = xhr.responseText

        verify(source.length > 0, "LoginPage.qml was read")
        verify(source.indexOf('objectName: "authSubmitButton"') !== -1,
               "Login submit has authSubmitButton objectName")
        verify(source.indexOf("enabled: !root.isBusy") !== -1,
               "Login submit button is disabled during busy state")
    }

    function test_register_submit_button_disabled_when_busy() {
        var xhr = new XMLHttpRequest()
        xhr.open("GET", Qt.resolvedUrl("../../../qml/pages/RegisterPage.qml"), false)
        xhr.send()
        var source = xhr.responseText

        verify(source.length > 0, "RegisterPage.qml was read")
        verify(source.indexOf('objectName: "authSubmitButton"') !== -1,
               "Register submit has authSubmitButton objectName")
        verify(source.indexOf("enabled: !root.isBusy") !== -1,
               "Register submit button is disabled during busy state")
    }
}
