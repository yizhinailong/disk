import QtQuick 2.15
import QtTest 1.15

TestCase {
    name: "DesktopLoginPage"
    id: testLoginPage

    // ── Source reading helper ───────────────────────────────────────────────

    function readLoginSource() {
        var xhr = new XMLHttpRequest()
        xhr.open("GET", Qt.resolvedUrl("../../../qml/pages/LoginPage.qml"), false)
        xhr.send()
        verify(xhr.responseText.length > 0, "LoginPage.qml was read")
        return xhr.responseText
    }

    // ── objectName hook contract ────────────────────────────────────────────
    // Each control has a stable objectName that tests and future shared-shell
    // code can locate programmatically.

    function test_login_root_objectName() {
        var source = readLoginSource()
        verify(source.indexOf('objectName: "authLoginPage"') !== -1,
               "Login page root has authLoginPage objectName")
    }

    function test_login_username_field_objectName() {
        var source = readLoginSource()
        verify(source.indexOf('objectName: "authUsernameField"') !== -1,
               "Username field has authUsernameField objectName")
    }

    function test_login_password_field_objectName() {
        var source = readLoginSource()
        verify(source.indexOf('objectName: "authPasswordField"') !== -1,
               "Password field has authPasswordField objectName")
    }

    function test_login_submit_button_objectName() {
        var source = readLoginSource()
        verify(source.indexOf('objectName: "authSubmitButton"') !== -1,
               "Submit button has authSubmitButton objectName")
    }

    function test_login_mode_switch_cta_objectName() {
        var source = readLoginSource()
        verify(source.indexOf('objectName: "authModeSwitchCta"') !== -1,
               "Mode-switch CTA has authModeSwitchCta objectName")
    }

    function test_login_error_label_objectName() {
        var source = readLoginSource()
        verify(source.indexOf('objectName: "authErrorLabel"') !== -1,
               "Error label has authErrorLabel objectName")
    }

    // ── Approved field contract ─────────────────────────────────────────────

    function test_login_has_username_field() {
        var source = readLoginSource()
        verify(source.indexOf("id: usernameField") !== -1,
               "Has usernameField with id")
        verify(source.indexOf('placeholderText: "用户名"') !== -1,
               "Username field has standard placeholder")
    }

    function test_login_has_password_field() {
        var source = readLoginSource()
        verify(source.indexOf("id: passwordField") !== -1,
               "Has passwordField with id")
        verify(source.indexOf("echoMode: TextInput.Password") !== -1,
               "Password field masks input")
    }

    function test_login_has_submit_button() {
        var source = readLoginSource()
        verify(source.indexOf("id: loginButton") !== -1,
               "Has loginButton with id")
        verify(source.indexOf('"登录"') !== -1 || source.indexOf("正在登录") !== -1,
               "Login button has appropriate label text")
    }

    function test_login_has_mode_switch_cta() {
        var source = readLoginSource()
        verify(source.indexOf('"注册"') !== -1,
               "Has Register CTA for mode switch")
        verify(source.indexOf("shellController.navigateToRegister()") !== -1,
               "Register CTA routes through shellController")
    }

    function test_login_has_error_display() {
        var source = readLoginSource()
        verify(source.indexOf("id: errorLabel") !== -1,
               "Has errorLabel for error feedback")
    }

    function test_login_has_busy_state() {
        var source = readLoginSource()
        verify(source.indexOf("property bool isBusy: false") !== -1,
               "Has isBusy property defaulting to false")
        verify(source.indexOf("!root.isBusy") !== -1,
               "Fields are disabled during busy state")
    }

    function test_login_has_resetState_function() {
        var source = readLoginSource()
        verify(source.indexOf("function resetState()") !== -1,
               "Has resetState function for cross-mode state cleanup")
        verify(source.indexOf('usernameField.text = ""') !== -1,
               "resetState clears username field")
        verify(source.indexOf('passwordField.text = ""') !== -1,
               "resetState clears password field")
        verify(source.indexOf('errorLabel.text = ""') !== -1,
               "resetState clears error label")
        verify(source.indexOf("usernameField.forceActiveFocus()") !== -1,
               "resetState focuses username field on mode entry")
    }

    function test_login_resetState_called_on_completed() {
        var source = readLoginSource()
        verify(source.indexOf("Component.onCompleted: resetState()") !== -1,
               "resetState is called when the component completes loading")
    }

    function test_login_submit_disabled_during_busy() {
        var source = readLoginSource()
        var submitMatch = source.match(/id: loginButton[\s\S]*?onClicked:/)
        verify(submitMatch !== null, "Found loginButton block with onClicked")
        verify(submitMatch[0].indexOf("enabled: !root.isBusy") !== -1,
               "Login submit button is disabled when isBusy")
    }

    function test_login_mode_switch_disabled_during_busy() {
        var source = readLoginSource()
        var ctaMatch = source.match(/id: registerModeCta[\s\S]*?onClicked:/)
        verify(ctaMatch !== null, "Found registerModeCta block with onClicked")
        verify(ctaMatch[0].indexOf("enabled: !root.isBusy") !== -1,
               "Mode-switch CTA is disabled when isBusy")
    }

    // ── Approved-only auth affordances ──────────────────────────────────────

    function test_login_only_uses_username_password_fields() {
        var source = readLoginSource()
        var textFieldIds = source.match(/id: \w+Field/g) || []
        compare(textFieldIds.length, 2,
                "Login page has exactly two input fields (username + password)")
        verify(textFieldIds.indexOf("id: usernameField") !== -1,
               "First field is usernameField")
        verify(textFieldIds.indexOf("id: passwordField") !== -1,
               "Second field is passwordField")
    }

    // ── Absence of excluded affordances ─────────────────────────────────────

    function test_login_no_qr_login() {
        var source = readLoginSource()
        verify(source.indexOf("QRCode") === -1, "No QR code login")
        verify(source.indexOf("Scan") === -1, "No scan-to-login")
    }

    function test_login_no_phone_sms_login() {
        var source = readLoginSource()
        verify(source.indexOf("phone") === -1, "No phone number login")
        verify(source.indexOf("SMS") === -1, "No SMS verification")
        verify(source.indexOf("verification code") === -1, "No verification code field")
        verify(source.indexOf("captcha") === -1, "No captcha")
    }

    function test_login_no_third_party_login() {
        var source = readLoginSource()
        verify(source.indexOf("WeChat") === -1, "No WeChat login")
        verify(source.indexOf("QQ") === -1, "No QQ login")
        verify(source.indexOf("Weibo") === -1, "No Weibo login")
        verify(source.indexOf("Google") === -1, "No Google login")
        verify(source.indexOf("GitHub") === -1, "No GitHub login")
        verify(source.indexOf("Baidu") === -1, "No Baidu login")
        verify(source.indexOf("Apple") === -1, "No Apple login")
        verify(source.indexOf("Facebook") === -1, "No Facebook login")
        verify(source.indexOf("Microsoft") === -1, "No Microsoft login")
    }

    function test_login_no_captcha() {
        var source = readLoginSource()
        verify(source.indexOf("captcha") === -1, "No captcha")
        verify(source.indexOf("Captcha") === -1, "No Captcha")
        verify(source.indexOf("slider") === -1, "No slider verification")
        verify(source.indexOf("puzzle") === -1, "No puzzle verification")
    }

    function test_login_no_marketing_blocks() {
        var source = readLoginSource()
        verify(source.indexOf("VIP") === -1, "No VIP promotion")
        verify(source.indexOf("Premium") === -1, "No Premium promotion")
        verify(source.indexOf("free trial") === -1, "No free trial promo")
        verify(source.indexOf("upgrade") === -1, "No upgrade prompt")
        verify(source.indexOf("discount") === -1, "No discount promo")
    }

    function test_login_no_cloud_drive_feature_cards() {
        var source = readLoginSource()
        verify(source.indexOf("storage") === -1, "No storage promo on login")
        verify(source.indexOf("sync") === -1, "No sync promo on login")
        verify(source.indexOf("backup") === -1, "No backup promo on login")
    }

    // ── Auth service integration contract ───────────────────────────────────

    function test_login_calls_auth_service_login() {
        var source = readLoginSource()
        verify(source.indexOf("authService.Login(") !== -1,
               "Login button calls authService.Login with credentials")
    }

    function test_login_handles_success_and_failure() {
        var source = readLoginSource()
        verify(source.indexOf("onLoginSuccess") !== -1,
               "Handles login success signal")
        verify(source.indexOf("onLoginFailure") !== -1,
               "Handles login failure signal")
        verify(source.indexOf("root.isBusy = false") !== -1,
               "Resets busy state on both outcomes")
    }

    function test_login_validates_empty_fields() {
        var source = readLoginSource()
        verify(source.indexOf('errorLabel.text = "请输入用户名"') !== -1,
               "Validates empty username with specific message")
        verify(source.indexOf('errorLabel.text = "请输入密码"') !== -1,
               "Validates empty password with specific message")
    }
}
