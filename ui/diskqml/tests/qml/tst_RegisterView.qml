import QtQuick
import QtQuick.Controls
import QtTest
import DiskAuth 1.0

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
        // Clear AuthViewModel state before each test
        AuthViewModel.clearForm()
        loader.sourceComponent = viewComponent
        verify(loader.item !== null)
    }

    function cleanup() {
        AuthViewModel.clearForm()
        loader.sourceComponent = undefined
    }

    function test_fieldBindings() {
        var view = loader.item.item
        
        var usernameInput = view.findChild("usernameInput")
        var emailInput = view.findChild("emailInput")
        var passwordInput = view.findChild("passwordInput")
        var confirmPasswordInput = view.findChild("confirmPasswordInput")
        
        verify(usernameInput)
        verify(emailInput)
        verify(passwordInput)
        verify(confirmPasswordInput)
        
        // Test two-way binding: AuthViewModel -> UI
        AuthViewModel.username = "testuser"
        compare(usernameInput.text, "testuser")
        
        AuthViewModel.email = "test@example.com"
        compare(emailInput.text, "test@example.com")
        
        AuthViewModel.password = "TestPass123"
        compare(passwordInput.text, "TestPass123")
        
        AuthViewModel.confirmPassword = "TestPass123"
        compare(confirmPasswordInput.text, "TestPass123")
        
        // Test two-way binding: UI -> AuthViewModel
        usernameInput.text = "newuser"
        compare(AuthViewModel.username, "newuser")
        
        emailInput.text = "new@test.com"
        compare(AuthViewModel.email, "new@test.com")
    }

    function test_validation_via_ViewModel() {
        var view = loader.item.item
        
        var usernameInput = view.findChild("usernameInput")
        var emailInput = view.findChild("emailInput")
        var passwordInput = view.findChild("passwordInput")
        var confirmPasswordInput = view.findChild("confirmPasswordInput")
        var submitButton = view.findChild("submitButton")
        
        // Initial state - form not valid
        verify(!AuthViewModel.isFormValid)
        verify(!submitButton.enabled)
        
        // Test username validation via AuthViewModel
        AuthViewModel.username = "abc" // too short
        verify(!AuthViewModel.isUsernameValid)
        
        AuthViewModel.username = "valid_user_123"
        verify(AuthViewModel.isUsernameValid)
        
        // Test email validation via AuthViewModel
        AuthViewModel.email = "invalid-email"
        verify(!AuthViewModel.isEmailValid)
        
        AuthViewModel.email = "test@example.com"
        verify(AuthViewModel.isEmailValid)
        
        // Test password validation via AuthViewModel
        AuthViewModel.password = "weakpass"
        verify(!AuthViewModel.isPasswordValid)
        
        AuthViewModel.password = "StrongPass123"
        verify(AuthViewModel.isPasswordValid)
        
        // Test confirm password - must match
        AuthViewModel.confirmPassword = "DifferentPass123"
        verify(!AuthViewModel.isConfirmPasswordValid)
        
        AuthViewModel.confirmPassword = "StrongPass123"
        verify(AuthViewModel.isConfirmPasswordValid)
        
        // Form should be valid now
        verify(AuthViewModel.isFormValid)
        verify(submitButton.enabled)
    }

    function test_loading_disables_inputs() {
        var view = loader.item.item
        var usernameInput = view.findChild("usernameInput")
        var emailInput = view.findChild("emailInput")
        var passwordInput = view.findChild("passwordInput")
        var confirmPasswordInput = view.findChild("confirmPasswordInput")
        var submitButton = view.findChild("submitButton")
        
        // Fill valid form
        AuthViewModel.username = "valid_user"
        AuthViewModel.email = "test@example.com"
        AuthViewModel.password = "StrongPass123"
        AuthViewModel.confirmPassword = "StrongPass123"
        
        verify(submitButton.enabled)
        verify(usernameInput.enabled)
        
        // Set loading via AuthViewModel
        AuthViewModel.loading = true
        verify(!submitButton.enabled)
        verify(!usernameInput.enabled)
        verify(!emailInput.enabled)
        verify(!passwordInput.enabled)
        verify(!confirmPasswordInput.enabled)
        
        AuthViewModel.loading = false
        verify(submitButton.enabled)
        verify(usernameInput.enabled)
    }

    function test_error_message_binding() {
        var view = loader.item.item
        var globalErrorLabel = view.findChild("globalErrorLabel")
        
        verify(globalErrorLabel)
        
        // Initial state - no error
        compare(AuthViewModel.errorMessage, "")
        verify(!globalErrorLabel.visible)
        
        // Set error via AuthViewModel
        AuthViewModel.errorMessage = "用户名已被注册"
        compare(globalErrorLabel.text, "用户名已被注册")
        verify(globalErrorLabel.visible)
        
        // Clear error
        AuthViewModel.errorMessage = ""
        verify(!globalErrorLabel.visible)
    }

    function test_success_signal() {
        var view = loader.item.item
        
        var signalEmitted = false
        var emittedUsername = ""
        var emittedEmail = ""
        
        view.registered.connect(function(username, email) {
            signalEmitted = true
            emittedUsername = username
            emittedEmail = email
        })
        
        // Fill valid form
        AuthViewModel.username = "valid_user"
        AuthViewModel.email = "test@example.com"
        AuthViewModel.password = "StrongPass123"
        AuthViewModel.confirmPassword = "StrongPass123"
        
        // Simulate successful registration by emitting from ViewModel
        // (In real app, this happens when registerUser() succeeds)
        AuthViewModel.emitRegistered()
        
        verify(signalEmitted)
        compare(emittedUsername, "valid_user")
        compare(emittedEmail, "test@example.com")
    }

    function test_clearForm() {
        // Fill form
        AuthViewModel.username = "testuser"
        AuthViewModel.email = "test@example.com"
        AuthViewModel.password = "TestPass123"
        AuthViewModel.confirmPassword = "TestPass123"
        AuthViewModel.errorMessage = "Some error"
        
        // Clear form
        AuthViewModel.clearForm()
        
        // Verify all cleared
        compare(AuthViewModel.username, "")
        compare(AuthViewModel.email, "")
        compare(AuthViewModel.password, "")
        compare(AuthViewModel.confirmPassword, "")
        compare(AuthViewModel.errorMessage, "")
    }
}
