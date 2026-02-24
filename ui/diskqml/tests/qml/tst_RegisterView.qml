import QtQuick
import QtQuick.Controls
import QtTest

TestCase {
    name: "RegisterViewTest"

    Component {
        id: viewComponent
        Loader {
            source: "../../qml/views/RegisterView.qml"
            onLoaded: {
                item.theme = {
                    primary: "#2196F3",
                    error: "#F44336",
                    background: "#FAFAFA",
                    textPrimary: "#212121",
                    textSecondary: "#757575",
                    border: "#E0E0E0",
                    xs: 4, sm: 8, md: 16, lg: 24, xl: 32,
                    h1: 24, body: 14, caption: 12,
                    inputHeight: 36, buttonHeight: 40,
                    radiusSmall: 4, radiusMedium: 8
                }
            }
        }
    }

    Loader {
        id: loader
    }

    function init() {
        loader.sourceComponent = viewComponent
        verify(loader.item !== null)
    }

    function cleanup() {
        loader.sourceComponent = undefined
    }

    function test_validation() {
        var view = loader.item.item
        
        var usernameInput = view.findChild("usernameInput")
        var emailInput = view.findChild("emailInput")
        var passwordInput = view.findChild("passwordInput")
        var confirmPasswordInput = view.findChild("confirmPasswordInput")
        var submitButton = view.findChild("submitButton")
        
        verify(usernameInput)
        verify(emailInput)
        verify(passwordInput)
        verify(confirmPasswordInput)
        verify(submitButton)
        
        // Initial state
        verify(!submitButton.enabled)
        
        // Test username
        usernameInput.text = "abc" // too short
        verify(!view.isUsernameValid())
        usernameInput.text = "valid_user_123"
        verify(view.isUsernameValid())
        
        // Test email
        emailInput.text = "invalid-email"
        verify(!view.isEmailValid())
        emailInput.text = "test@example.com"
        verify(view.isEmailValid())
        
        // Test password
        passwordInput.text = "weakpass"
        verify(!view.isPasswordValid())
        passwordInput.text = "StrongPass123"
        verify(view.isPasswordValid())
        
        // Test confirm password
        confirmPasswordInput.text = "DifferentPass123"
        verify(!view.isConfirmPasswordValid())
        confirmPasswordInput.text = "StrongPass123"
        verify(view.isConfirmPasswordValid())
        
        // Form should be valid now
        verify(view.isFormValid())
        verify(submitButton.enabled)
    }

    function test_loading_disables_submit() {
        var view = loader.item.item
        var usernameInput = view.findChild("usernameInput")
        var emailInput = view.findChild("emailInput")
        var passwordInput = view.findChild("passwordInput")
        var confirmPasswordInput = view.findChild("confirmPasswordInput")
        var submitButton = view.findChild("submitButton")
        
        // Fill valid form
        usernameInput.text = "valid_user"
        emailInput.text = "test@example.com"
        passwordInput.text = "StrongPass123"
        confirmPasswordInput.text = "StrongPass123"
        
        verify(submitButton.enabled)
        
        // Set loading
        view.loading = true
        verify(!submitButton.enabled)
        verify(!usernameInput.enabled)
        
        view.loading = false
        verify(submitButton.enabled)
        verify(usernameInput.enabled)
    }

    function test_error_mapping() {
        var view = loader.item.item
        var submitButton = view.findChild("submitButton")
        var globalErrorLabel = view.findChild("globalErrorLabel")
        
        var currentResponse = {}
        
        view.httpClient = {
            register: function(data, callback) {
                callback(currentResponse)
            }
        }
        
        // Fill valid form
        view.findChild("usernameInput").text = "valid_user"
        view.findChild("emailInput").text = "test@example.com"
        view.findChild("passwordInput").text = "StrongPass123"
        view.findChild("confirmPasswordInput").text = "StrongPass123"
        
        // Test 40001
        currentResponse = {ok: false, code: 40001}
        submitButton.clicked()
        compare(view.globalError, "用户名已被注册")
        
        // Test 40002
        currentResponse = {ok: false, code: 40002}
        submitButton.clicked()
        compare(view.globalError, "邮箱已被注册")
    }

    function test_success_signal() {
        var view = loader.item.item
        var submitButton = view.findChild("submitButton")
        
        view.httpClient = {
            register: function(data, callback) {
                callback({ok: true, code: 0})
            }
        }
        
        var signalEmitted = false
        var emittedUsername = ""
        var emittedEmail = ""
        
        view.registered.connect(function(username, email) {
            signalEmitted = true
            emittedUsername = username
            emittedEmail = email
        })
        
        // Fill valid form
        view.findChild("usernameInput").text = "valid_user"
        view.findChild("emailInput").text = "test@example.com"
        view.findChild("passwordInput").text = "StrongPass123"
        view.findChild("confirmPasswordInput").text = "StrongPass123"
        
        submitButton.clicked()
        
        verify(signalEmitted)
        compare(emittedUsername, "valid_user")
        compare(emittedEmail, "test@example.com")
    }
}
