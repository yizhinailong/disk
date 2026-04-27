import QtQuick 2.15
import QtTest 1.15

TestCase {
    name: "DesktopRegisterPage"
    id: testRegisterPage

    // ── Source reading helper ───────────────────────────────────────────────

    function readRegisterSource() {
        var xhr = new XMLHttpRequest()
        xhr.open("GET", Qt.resolvedUrl("../../../qml/pages/RegisterPage.qml"), false)
        xhr.send()
        verify(xhr.responseText.length > 0, "RegisterPage.qml was read")
        return xhr.responseText
    }

    // ── objectName hook contract ────────────────────────────────────────────
    // Each control has a stable objectName for test lookups and future
    // shared-shell reuse.

    function test_register_root_objectName() {
        var source = readRegisterSource()
        verify(source.indexOf('objectName: "authRegisterPage"') !== -1,
               "Register page root has authRegisterPage objectName")
    }

    function test_register_username_field_objectName() {
        var source = readRegisterSource()
        verify(source.indexOf('objectName: "authUsernameField"') !== -1,
               "Username field has authUsernameField objectName")
    }

    function test_register_email_field_objectName() {
        var source = readRegisterSource()
        verify(source.indexOf('objectName: "authEmailField"') !== -1,
               "Email field has authEmailField objectName")
    }

    function test_register_password_field_objectName() {
        var source = readRegisterSource()
        verify(source.indexOf('objectName: "authPasswordField"') !== -1,
               "Password field has authPasswordField objectName")
    }

    function test_register_confirm_password_field_objectName() {
        var source = readRegisterSource()
        verify(source.indexOf('objectName: "authConfirmPasswordField"') !== -1,
               "Confirm-password field has authConfirmPasswordField objectName")
    }

    function test_register_submit_button_objectName() {
        var source = readRegisterSource()
        verify(source.indexOf('objectName: "authSubmitButton"') !== -1,
               "Submit button has authSubmitButton objectName")
    }

    function test_register_mode_switch_cta_objectName() {
        var source = readRegisterSource()
        verify(source.indexOf('objectName: "authModeSwitchCta"') !== -1,
               "Mode-switch CTA has authModeSwitchCta objectName")
    }

    function test_register_error_label_objectName() {
        var source = readRegisterSource()
        verify(source.indexOf('objectName: "authErrorLabel"') !== -1,
               "Error label has authErrorLabel objectName")
    }

    function test_register_success_timer_objectName() {
        var source = readRegisterSource()
        verify(source.indexOf('objectName: "authRegisterSuccessTimer"') !== -1,
               "Success timer has authRegisterSuccessTimer objectName")
    }

    // ── Approved field contract ─────────────────────────────────────────────

    function test_register_has_username_field() {
        var source = readRegisterSource()
        verify(source.indexOf("id: usernameField") !== -1,
               "Has usernameField with id")
        verify(source.indexOf('placeholderText: "Username"') !== -1,
               "Username field has standard placeholder")
        verify(source.indexOf("maximumLength: 32") !== -1,
               "Username field has maximum length constraint")
    }

    function test_register_has_email_field() {
        var source = readRegisterSource()
        verify(source.indexOf("id: emailField") !== -1,
               "Has emailField with id")
        verify(source.indexOf('placeholderText: "Email"') !== -1,
               "Email field has standard placeholder")
        verify(source.indexOf("Qt.ImhEmailCharactersOnly") !== -1,
               "Email field uses email input method hints")
    }

    function test_register_has_password_field() {
        var source = readRegisterSource()
        verify(source.indexOf("id: passwordField") !== -1,
               "Has passwordField with id")
        verify(source.indexOf("echoMode: TextInput.Password") !== -1,
               "Password field masks input")
        verify(source.indexOf("maximumLength: 64") !== -1,
               "Password field has maximum length constraint")
    }

    function test_register_has_confirm_password_field() {
        var source = readRegisterSource()
        verify(source.indexOf("id: confirmPasswordField") !== -1,
               "Has confirmPasswordField with id")
        verify(source.indexOf('placeholderText: "Confirm password"') !== -1,
               "Confirm password field has standard placeholder")
        verify(source.indexOf("echoMode: TextInput.Password") !== -1,
               "Confirm password field masks input")
    }

    function test_register_has_submit_button() {
        var source = readRegisterSource()
        verify(source.indexOf("id: registerButton") !== -1,
               "Has registerButton with id")
        verify(source.indexOf('"Create account"') !== -1 || source.indexOf("Creating account") !== -1,
               "Register button has appropriate label text")
    }

    function test_register_has_mode_switch_cta() {
        var source = readRegisterSource()
        verify(source.indexOf('"Back to login"') !== -1,
               "Has Back-to-login CTA for mode switch")
        verify(source.indexOf("shellController.navigateToLogin()") !== -1,
               "Back-to-login CTA routes through shellController")
    }

    function test_register_has_error_display() {
        var source = readRegisterSource()
        verify(source.indexOf("id: messageLabel") !== -1,
               "Has messageLabel for error/success feedback")
    }

    function test_register_has_busy_state() {
        var source = readRegisterSource()
        verify(source.indexOf("property bool isBusy: false") !== -1,
               "Has isBusy property defaulting to false")
        verify(source.indexOf("!root.isBusy") !== -1,
               "Fields are disabled during busy state")
    }

    function test_register_has_resetState_function() {
        var source = readRegisterSource()
        verify(source.indexOf("function resetState()") !== -1,
               "Has resetState function for cross-mode state cleanup")
        verify(source.indexOf('usernameField.text = ""') !== -1,
               "resetState clears username field")
        verify(source.indexOf('emailField.text = ""') !== -1,
               "resetState clears email field")
        verify(source.indexOf('passwordField.text = ""') !== -1,
               "resetState clears password field")
        verify(source.indexOf('confirmPasswordField.text = ""') !== -1,
               "resetState clears confirm password field")
        verify(source.indexOf('messageLabel.text = ""') !== -1,
               "resetState clears message label")
        verify(source.indexOf("returnToLoginTimer.stop()") !== -1,
               "resetState stops return-to-login timer")
        verify(source.indexOf("usernameField.forceActiveFocus()") !== -1,
               "resetState focuses username field on mode entry")
    }

    function test_register_resetState_called_on_completed() {
        var source = readRegisterSource()
        verify(source.indexOf("Component.onCompleted: resetState()") !== -1,
               "resetState is called when the component completes loading")
    }

    function test_register_submit_disabled_during_busy() {
        var source = readRegisterSource()
        var submitMatch = source.match(/id: registerButton[\s\S]*?onClicked:/)
        verify(submitMatch !== null, "Found registerButton block with onClicked")
        verify(submitMatch[0].indexOf("enabled: !root.isBusy") !== -1,
               "Register submit button is disabled when isBusy")
    }

    function test_register_mode_switch_disabled_during_busy() {
        var source = readRegisterSource()
        var ctaMatch = source.match(/id: loginModeCta[\s\S]*?onClicked:/)
        verify(ctaMatch !== null, "Found loginModeCta block with onClicked")
        verify(ctaMatch[0].indexOf("enabled: !root.isBusy") !== -1,
               "Mode-switch CTA is disabled when isBusy")
    }

    // ── Approved-only auth affordances ──────────────────────────────────────

    function test_register_only_uses_approved_fields() {
        var source = readRegisterSource()
        var textFieldIds = source.match(/id: \w+Field/g) || []
        compare(textFieldIds.length, 4,
                "Register page has exactly four input fields (username + email + password + confirm)")
        verify(textFieldIds.indexOf("id: usernameField") !== -1, "Has usernameField")
        verify(textFieldIds.indexOf("id: emailField") !== -1, "Has emailField")
        verify(textFieldIds.indexOf("id: passwordField") !== -1, "Has passwordField")
        verify(textFieldIds.indexOf("id: confirmPasswordField") !== -1, "Has confirmPasswordField")
    }

    // ── Validation contract ─────────────────────────────────────────────────

    function test_register_validates_username() {
        var source = readRegisterSource()
        verify(source.indexOf("function validateUsername(") !== -1,
               "Has validateUsername function")
        verify(source.indexOf("4-32 characters") !== -1,
               "Username validation specifies 4-32 character range")
        verify(source.indexOf("/^[A-Za-z0-9_]+$/") !== -1,
               "Username validation restricts to alphanumeric + underscore")
    }

    function test_register_validates_email() {
        var source = readRegisterSource()
        verify(source.indexOf("function validateEmail(") !== -1,
               "Has validateEmail function")
        verify(source.indexOf("@") !== -1,
               "Email validation checks for @ symbol")
    }

    function test_register_validates_password() {
        var source = readRegisterSource()
        verify(source.indexOf("function validatePassword(") !== -1,
               "Has validatePassword function")
        verify(source.indexOf("8-64 characters") !== -1,
               "Password validation specifies 8-64 character range")
        verify(source.indexOf("uppercase") !== -1,
               "Password validation requires uppercase")
        verify(source.indexOf("lowercase") !== -1,
               "Password validation requires lowercase")
    }

    function test_register_validates_password_match() {
        var source = readRegisterSource()
        verify(source.indexOf("password !== confirmPasswordField.text") !== -1,
               "Validates that password and confirm password match")
        verify(source.indexOf("do not match") !== -1,
               "Provides mismatch error message")
    }

    // ── Register success timer contract ─────────────────────────────────────

    function test_register_has_success_timer() {
        var source = readRegisterSource()
        verify(source.indexOf("id: returnToLoginTimer") !== -1,
               "Has returnToLoginTimer for post-register navigation")
        verify(source.indexOf("interval: 1200") !== -1,
               "Success timer has appropriate delay (1200ms)")
        verify(source.indexOf("shellController.navigateToLogin()") !== -1,
               "Success timer navigates to login on trigger")
    }

    function test_register_starts_success_timer_on_success() {
        var source = readRegisterSource()
        verify(source.indexOf("returnToLoginTimer.start()") !== -1,
               "onRegisterSuccess starts the return-to-login timer")
    }

    // ── Absence of excluded affordances ─────────────────────────────────────

    function test_register_no_qr_login() {
        var source = readRegisterSource()
        verify(source.indexOf("QRCode") === -1, "No QR code")
        verify(source.indexOf("Scan") === -1, "No scan functionality")
    }

    function test_register_no_phone_sms_login() {
        var source = readRegisterSource()
        verify(source.indexOf("phone") === -1, "No phone number field")
        verify(source.indexOf("SMS") === -1, "No SMS verification")
        verify(source.indexOf("verification code") === -1, "No verification code")
        verify(source.indexOf("captcha") === -1, "No captcha")
    }

    function test_register_no_third_party_login() {
        var source = readRegisterSource()
        verify(source.indexOf("WeChat") === -1, "No WeChat")
        verify(source.indexOf("QQ") === -1, "No QQ")
        verify(source.indexOf("Weibo") === -1, "No Weibo")
        verify(source.indexOf("Google") === -1, "No Google")
        verify(source.indexOf("GitHub") === -1, "No GitHub")
        verify(source.indexOf("Baidu") === -1, "No Baidu")
        verify(source.indexOf("Apple") === -1, "No Apple")
        verify(source.indexOf("Facebook") === -1, "No Facebook")
        verify(source.indexOf("Microsoft") === -1, "No Microsoft")
    }

    function test_register_no_captcha() {
        var source = readRegisterSource()
        verify(source.indexOf("captcha") === -1, "No captcha")
        verify(source.indexOf("Captcha") === -1, "No Captcha")
        verify(source.indexOf("slider") === -1, "No slider verification")
        verify(source.indexOf("puzzle") === -1, "No puzzle verification")
    }

    function test_register_no_marketing_blocks() {
        var source = readRegisterSource()
        verify(source.indexOf("VIP") === -1, "No VIP promotion")
        verify(source.indexOf("Premium") === -1, "No Premium promotion")
        verify(source.indexOf("free trial") === -1, "No free trial")
        verify(source.indexOf("upgrade") === -1, "No upgrade prompt")
        verify(source.indexOf("discount") === -1, "No discount promo")
    }

    function test_register_no_cloud_drive_feature_cards() {
        var source = readRegisterSource()
        verify(source.indexOf("cloud drive") === -1, "No cloud-drive feature card")
        verify(source.indexOf("sync") === -1, "No sync promo")
        verify(source.indexOf("backup") === -1, "No backup promo")
    }

    // ── Auth service integration contract ───────────────────────────────────

    function test_register_calls_auth_service_register() {
        var source = readRegisterSource()
        verify(source.indexOf("authService.Register(") !== -1,
               "Register button calls authService.Register with credentials")
    }

    function test_register_handles_success_and_failure() {
        var source = readRegisterSource()
        verify(source.indexOf("onRegisterSuccess") !== -1,
               "Handles register success signal")
        verify(source.indexOf("onRegisterFailure") !== -1,
               "Handles register failure signal")
        verify(source.indexOf("root.isBusy = false") !== -1,
               "Resets busy state on both outcomes")
    }

    function test_register_success_shows_confirmation_message() {
        var source = readRegisterSource()
        verify(source.indexOf("Account created") !== -1,
               "Shows account-created confirmation message on success")
        verify(source.indexOf("theme.successTextColor") !== -1,
               "Success message uses shared success color token")
    }
}
