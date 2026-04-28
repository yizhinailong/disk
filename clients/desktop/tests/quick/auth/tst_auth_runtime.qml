import QtQuick 2.15
import QtQuick.Controls
import QtTest 1.15

TestCase {
    id: testRoot

    name: "DesktopAuthRuntime"

    // ── Helpers ────────────────────────────────────────────────────────────────

    property var _created: []

    function cleanup() {
        for (var i = 0; i < _created.length; i++) {
            if (_created[i]) _created[i].destroy()
        }
        _created = []
    }

    function register(obj) {
        _created.push(obj)
        return obj
    }

    // ── URL construction ────────────────────────────────────────────────────
    // Qt.resolvedUrl() with dynamic string concatenation returns empty in test
    // functions.  Use the proven-static "." resolution to obtain a base
    // directory, then normalize "../" segments manually.

    function sourceUrl(relPath) {
        var base = Qt.resolvedUrl(".").toString()
        var raw = base + "../../../qml/" + relPath
        return _normalizeFileUrl(raw)
    }

    function _normalizeFileUrl(url) {
        var idx = url.indexOf("://")
        var sep = idx >= 0 ? idx + 3 : 0
        var prefix = url.substring(0, sep)
        var path   = url.substring(sep)
        var parts  = path.split("/")
        var stack  = []
        for (var i = 0; i < parts.length; i++) {
            if (parts[i] === "..") {
                if (stack.length > 0) stack.pop()
            } else if (parts[i] !== "" && parts[i] !== ".") {
                stack.push(parts[i])
            }
        }
        return prefix + stack.join("/")
    }

    // ── Stub injection ──────────────────────────────────────────────────────
    // The C++ setup object (QuickTestSetup) is exposed as a context property
    // by quick_test_main_with_setup.  Calling inject() sets authService,
    // shellController, and sessionStore on the root QQmlContext so that
    // runtime-instantiated auth pages resolve these identifiers without
    // ReferenceError.

    function _tryInjectStubs() {
        var s = null
        try { s = _q_quicktest_setup } catch(e) {}
        if (!s) try { s = setup } catch(e) {}
        if (s && typeof s.inject === "function") s.inject()
    }

    function initTestCase() {
        _tryInjectStubs()
    }

    // ── Component / instance helpers ────────────────────────────────────────

    function loadComponent(relPath) {
        var url = sourceUrl(relPath)
        var comp = Qt.createComponent(url)
        verify(comp !== null, "Component object created for " + relPath)
        if (comp.status === Component.Error) {
            verify(false, "Component error for " + relPath + ": " + comp.errorString())
        }
        if (comp.status === Component.Loading) {
            wait(500)
        }
        if (comp.status === Component.Error) {
            verify(false, "Component error after wait for " + relPath + ": " + comp.errorString())
        }
        verify(comp.status === Component.Ready,
               "Component ready for " + relPath + " (status=" + comp.status
               + ", url=" + url + ")")
        return comp
    }

    function createPage(relPath) {
        var comp = loadComponent(relPath)
        var obj = comp.createObject(testRoot)
        verify(obj !== null, "Instance created for " + relPath)
        register(obj)
        return obj
    }

    // ── Object tree traversal ───────────────────────────────────────────────

    function findChildByName(item, name) {
        if (!item) return null
        if (item.objectName === name) return item

        // Traverse Loader's loaded item
        if (item.item !== undefined && item.item !== null
            && typeof item.item === "object") {
            var found = findChildByName(item.item, name)
            if (found) return found
        }

        // Traverse ApplicationWindow / Window contentItem
        if (item.contentItem !== undefined && item.contentItem !== null
            && item.contentItem !== item) {
            found = findChildByName(item.contentItem, name)
            if (found) return found
        }

        // Traverse visual children
        if (item.children) {
            for (var i = 0; i < item.children.length; i++) {
                found = findChildByName(item.children[i], name)
                if (found) return found
            }
        }
        return null
    }

    function findObjectInData(item, name) {
        if (!item) return null
        if (item.data !== undefined && item.data !== null) {
            for (var i = 0; i < item.data.length; i++) {
                var obj = item.data[i]
                if (obj && obj.objectName === name) return obj
            }
        }
        // Also check Loader.item and contentItem subtrees
        if (item.item !== undefined && item.item !== null
            && typeof item.item === "object") {
            var found = findObjectInData(item.item, name)
            if (found) return found
        }
        if (item.contentItem !== undefined && item.contentItem !== null
            && item.contentItem !== item) {
            found = findObjectInData(item.contentItem, name)
            if (found) return found
        }
        if (item.children) {
            for (var j = 0; j < item.children.length; j++) {
                found = findObjectInData(item.children[j], name)
                if (found) return found
            }
        }
        return null
    }

    // ── LoginPage runtime behavior ─────────────────────────────────────────────

    function test_login_resetState_resets_isBusy() {
        var page = createPage("pages/LoginPage.qml")
        page.isBusy = true
        verify(page.isBusy === true, "isBusy was set to true")
        page.resetState()
        verify(page.isBusy === false, "resetState resets isBusy to false")
    }

    function test_login_isBusy_disables_username_field() {
        var page = createPage("pages/LoginPage.qml")
        var field = findChildByName(page, "authUsernameField")
        verify(field !== null, "Found username field by objectName")
        verify(field.enabled === true, "Username field enabled when not busy")
        page.isBusy = true
        verify(field.enabled === false, "Username field disabled when busy")
        page.isBusy = false
        verify(field.enabled === true, "Username field re-enabled when not busy")
    }

    function test_login_isBusy_disables_password_field() {
        var page = createPage("pages/LoginPage.qml")
        var field = findChildByName(page, "authPasswordField")
        verify(field !== null, "Found password field by objectName")
        verify(field.enabled === true, "Password field enabled when not busy")
        page.isBusy = true
        verify(field.enabled === false, "Password field disabled when busy")
        page.isBusy = false
        verify(field.enabled === true, "Password field re-enabled when not busy")
    }

    function test_login_isBusy_disables_submit_button() {
        var page = createPage("pages/LoginPage.qml")
        var btn = findChildByName(page, "authSubmitButton")
        verify(btn !== null, "Found submit button by objectName")
        verify(btn.enabled === true, "Submit enabled when not busy")
        page.isBusy = true
        verify(btn.enabled === false, "Submit disabled when busy")
        page.isBusy = false
        verify(btn.enabled === true, "Submit re-enabled when not busy")
    }

    function test_login_isBusy_disables_mode_switch_cta() {
        var page = createPage("pages/LoginPage.qml")
        var cta = findChildByName(page, "authModeSwitchCta")
        verify(cta !== null, "Found mode-switch CTA by objectName")
        verify(cta.enabled === true, "Mode-switch CTA enabled when not busy")
        page.isBusy = true
        verify(cta.enabled === false, "Mode-switch CTA disabled when busy")
        page.isBusy = false
        verify(cta.enabled === true, "Mode-switch CTA re-enabled when not busy")
    }

    function test_login_isBusy_changes_submit_text() {
        var page = createPage("pages/LoginPage.qml")
        var btn = findChildByName(page, "authSubmitButton")
        verify(btn !== null, "Found submit button by objectName")
        var idleText = btn.text
        verify(idleText.indexOf("Login") !== -1, "Idle text contains Login: " + idleText)
        page.isBusy = true
        var busyText = btn.text
        verify(busyText !== idleText, "Button text changes when busy")
        verify(busyText.indexOf("Signing in") !== -1,
               "Busy text indicates signing in: " + busyText)
        page.isBusy = false
        verify(btn.text === idleText, "Button text restores when not busy")
    }

    function test_login_root_has_isBusy_property() {
        var page = createPage("pages/LoginPage.qml")
        verify(page.isBusy === false, "isBusy defaults to false")
    }

    function test_login_root_objectName_at_runtime() {
        var page = createPage("pages/LoginPage.qml")
        verify(page.objectName === "authLoginPage",
               "Runtime instance has authLoginPage objectName")
    }

    // ── RegisterPage runtime behavior ─────────────────────────────────────────

    function test_register_resetState_resets_isBusy() {
        var page = createPage("pages/RegisterPage.qml")
        page.isBusy = true
        verify(page.isBusy === true, "isBusy was set to true")
        page.resetState()
        verify(page.isBusy === false, "resetState resets isBusy to false")
    }

    function test_register_isBusy_disables_username_field() {
        var page = createPage("pages/RegisterPage.qml")
        var field = findChildByName(page, "authUsernameField")
        verify(field !== null, "Found username field by objectName")
        verify(field.enabled === true, "Username enabled when not busy")
        page.isBusy = true
        verify(field.enabled === false, "Username disabled when busy")
        page.isBusy = false
        verify(field.enabled === true, "Username re-enabled when not busy")
    }

    function test_register_isBusy_disables_email_field() {
        var page = createPage("pages/RegisterPage.qml")
        var field = findChildByName(page, "authEmailField")
        verify(field !== null, "Found email field by objectName")
        verify(field.enabled === true, "Email enabled when not busy")
        page.isBusy = true
        verify(field.enabled === false, "Email disabled when busy")
        page.isBusy = false
        verify(field.enabled === true, "Email re-enabled when not busy")
    }

    function test_register_isBusy_disables_password_field() {
        var page = createPage("pages/RegisterPage.qml")
        var field = findChildByName(page, "authPasswordField")
        verify(field !== null, "Found password field by objectName")
        verify(field.enabled === true, "Password enabled when not busy")
        page.isBusy = true
        verify(field.enabled === false, "Password disabled when busy")
        page.isBusy = false
        verify(field.enabled === true, "Password re-enabled when not busy")
    }

    function test_register_isBusy_disables_confirm_password_field() {
        var page = createPage("pages/RegisterPage.qml")
        var field = findChildByName(page, "authConfirmPasswordField")
        verify(field !== null, "Found confirm-password field by objectName")
        verify(field.enabled === true, "Confirm password enabled when not busy")
        page.isBusy = true
        verify(field.enabled === false, "Confirm password disabled when busy")
        page.isBusy = false
        verify(field.enabled === true, "Confirm password re-enabled when not busy")
    }

    function test_register_isBusy_disables_submit_button() {
        var page = createPage("pages/RegisterPage.qml")
        var btn = findChildByName(page, "authSubmitButton")
        verify(btn !== null, "Found submit button by objectName")
        verify(btn.enabled === true, "Submit enabled when not busy")
        page.isBusy = true
        verify(btn.enabled === false, "Submit disabled when busy")
        page.isBusy = false
        verify(btn.enabled === true, "Submit re-enabled when not busy")
    }

    function test_register_isBusy_disables_mode_switch_cta() {
        var page = createPage("pages/RegisterPage.qml")
        var cta = findChildByName(page, "authModeSwitchCta")
        verify(cta !== null, "Found mode-switch CTA by objectName")
        verify(cta.enabled === true, "Mode-switch CTA enabled when not busy")
        page.isBusy = true
        verify(cta.enabled === false, "Mode-switch CTA disabled when busy")
        page.isBusy = false
        verify(cta.enabled === true, "Mode-switch CTA re-enabled when not busy")
    }

    function test_register_isBusy_changes_submit_text() {
        var page = createPage("pages/RegisterPage.qml")
        var btn = findChildByName(page, "authSubmitButton")
        verify(btn !== null, "Found submit button by objectName")
        var idleText = btn.text
        verify(idleText.indexOf("Create account") !== -1,
               "Idle text contains Create account: " + idleText)
        page.isBusy = true
        var busyText = btn.text
        verify(busyText !== idleText, "Button text changes when busy")
        verify(busyText.indexOf("Creating account") !== -1,
               "Busy text indicates creating: " + busyText)
        page.isBusy = false
        verify(btn.text === idleText, "Button text restores when not busy")
    }

    function test_register_resetState_stops_success_timer() {
        var page = createPage("pages/RegisterPage.qml")
        var timer = findObjectInData(page, "authRegisterSuccessTimer")
        verify(timer !== null, "Found returnToLoginTimer by objectName")
        timer.interval = 50
        timer.start()
        verify(timer.running === true, "Timer started")
        page.resetState()
        verify(timer.running === false, "resetState stops the success timer")
    }

    function test_register_root_objectName_at_runtime() {
        var page = createPage("pages/RegisterPage.qml")
        verify(page.objectName === "authRegisterPage",
               "Runtime instance has authRegisterPage objectName")
    }

    // ── AuthShell runtime behavior ─────────────────────────────────────────────

    function test_authshell_title_is_login_by_default() {
        var shell = createPage("shells/AuthShell.qml")
        verify(shell.title.indexOf("Login") !== -1,
               "Default title contains Login: " + shell.title)
    }

    function test_authshell_title_syncs_to_register() {
        var shell = createPage("shells/AuthShell.qml")
        shell.authMode = "register"
        verify(shell.title.indexOf("Register") !== -1,
               "Title contains Register after mode switch: " + shell.title)
    }

    function test_authshell_title_syncs_back_to_login() {
        var shell = createPage("shells/AuthShell.qml")
        shell.authMode = "register"
        shell.authMode = "login"
        verify(shell.title.indexOf("Login") !== -1,
               "Title contains Login after switching back: " + shell.title)
    }

    function test_authshell_busy_tracks_loaded_page() {
        var shell = createPage("shells/AuthShell.qml")
        wait(200)
        verify(shell.busy === false, "Not busy by default")
        var loginPage = findChildByName(shell, "authLoginPage")
        verify(loginPage !== null, "Found loaded LoginPage by objectName")
        loginPage.isBusy = true
        verify(shell.busy === true, "Shell busy reflects page isBusy=true")
        loginPage.isBusy = false
        verify(shell.busy === false, "Shell busy reflects page isBusy=false")
    }

    function test_authshell_mode_switch_guard_blocks_when_busy() {
        var shell = createPage("shells/AuthShell.qml")
        wait(200)
        var loginPage = findChildByName(shell, "authLoginPage")
        verify(loginPage !== null, "Found loaded LoginPage")
        loginPage.isBusy = true
        verify(shell.busy === true, "Shell reports busy before guard test")
        verify(shell.authMode === "login", "authMode is login before attempted switch")
        shell.authMode = "register"
        verify(shell.authMode === "login",
               "authMode stays login when busy — mode switch guard blocked it (got: "
               + shell.authMode + ")")
        loginPage.isBusy = false
        shell.authMode = "register"
        verify(shell.authMode === "register",
               "authMode switches to register when not busy")
    }

    function test_authshell_width_and_height_at_runtime() {
        var shell = createPage("shells/AuthShell.qml")
        verify(shell.width === 1024, "AuthShell width is 1024")
        verify(shell.height === 768, "AuthShell height is 768")
    }

    function test_authshell_switches_to_register_page() {
        var shell = createPage("shells/AuthShell.qml")
        shell.authMode = "register"
        wait(200)
        var regPage = findChildByName(shell, "authRegisterPage")
        verify(regPage !== null, "Found loaded RegisterPage after mode switch")
    }

    // ── AuthCard geometry helpers ───────────────────────────────────────────

    /**
     * Find the card surface rectangle inside an AuthCard instance.
     * The card surface is identified by its objectName "authCardSurface".
     */
    function _findCardSurface(authCard) {
        return findChildByName(authCard, "authCardSurface")
    }

    /**
     * Read AuthCard.qml source via synchronous XHR.
     * Uses the same Qt.resolvedUrl path pattern proven in other quick tests.
     */
    function readAuthCardSource() {
        var xhr = new XMLHttpRequest()
        xhr.open("GET", Qt.resolvedUrl("../../../qml/components/auth/AuthCard.qml"), false)
        xhr.send()
        verify(xhr.responseText.length > 0, "AuthCard.qml was read")
        return xhr.responseText
    }

    // ── AuthCard geometry assertions ────────────────────────────────────────

    function test_authcard_instantiates() {
        var card = createPage("components/auth/AuthCard.qml")
        verify(card !== null, "AuthCard instance created")
    }

    function test_authcard_no_shadow_surface() {
        var card = createPage("components/auth/AuthCard.qml")
        var shadow = findChildByName(card, "authCardShadowSurface")
        verify(shadow === null,
               "AuthCard must not contain a shadow surface (authCardShadowSurface)")
    }

    function test_authcard_has_card_surface() {
        var card = createPage("components/auth/AuthCard.qml")
        var surface = _findCardSurface(card)
        verify(surface !== null,
               "Found card surface (authCardSurface) inside AuthCard")
    }

    function test_authcard_has_accent_bar() {
        var source = readAuthCardSource()

        // The accent bar is a Rectangle inside cardSurface, anchored to the top,
        // with a gradient referencing theme accent colors and a themed height.
        verify(source.indexOf("anchors.top: parent.top") !== -1,
               "AuthCard has a child anchored to parent top (accent bar position)")
        verify(source.indexOf("cardAccentHeight") !== -1,
               "AuthCard references cardAccentHeight for accent bar sizing")
        verify(source.indexOf("gradient:") !== -1,
               "AuthCard has a gradient element (accent bar fill)")
        verify(source.indexOf("cardAccentStartColor") !== -1,
               "Accent bar gradient references cardAccentStartColor theme token")
        verify(source.indexOf("cardAccentEndColor") !== -1,
               "Accent bar gradient references cardAccentEndColor theme token")

        // Structural guard: the accent bar sits inside cardSurface (between
        // cardSurface's opening and the contentLayout ColumnLayout).  Extract
        // the cardSurface block to verify these tokens co-occur inside it.
        var cardSurfaceStart = source.indexOf('objectName: "authCardSurface"')
        verify(cardSurfaceStart !== -1, "Found cardSurface objectName in source")

        var contentLayoutIdx = source.indexOf("id: contentLayout")
        verify(contentLayoutIdx !== -1, "Found contentLayout id in source")
        verify(contentLayoutIdx > cardSurfaceStart,
               "contentLayout appears after cardSurface declaration")

        var betweenSurfaceAndLayout = source.substring(cardSurfaceStart, contentLayoutIdx)
        verify(betweenSurfaceAndLayout.indexOf("anchors.top: parent.top") !== -1,
               "Top-anchored Rectangle sits inside cardSurface before contentLayout")
        verify(betweenSurfaceAndLayout.indexOf("gradient:") !== -1,
               "Gradient sits inside cardSurface before contentLayout")
    }

    function test_authcard_surface_centered_x() {
        var card = createPage("components/auth/AuthCard.qml")
        var surface = _findCardSurface(card)
        verify(surface !== null,
               "Found card surface for x-centering check")

        wait(100)

        var rootCenterX = card.x + card.width / 2
        var surfaceCenterX = surface.x + surface.width / 2

        var deltaX = Math.abs(surfaceCenterX - rootCenterX)
        verify(deltaX <= 1,
               "Card surface horizontal centerline within 1px of root centerline"
               + " — rootCenterX=" + rootCenterX
               + " surfaceCenterX=" + surfaceCenterX
               + " deltaX=" + deltaX)
    }

    function test_authcard_surface_centered_y() {
        var card = createPage("components/auth/AuthCard.qml")
        var surface = _findCardSurface(card)
        verify(surface !== null,
               "Found card surface for y-centering check")

        wait(100)

        var rootCenterY = card.y + card.height / 2
        var surfaceCenterY = surface.y + surface.height / 2

        var deltaY = Math.abs(surfaceCenterY - rootCenterY)
        verify(deltaY <= 1,
               "Card surface vertical centerline within 1px of root centerline"
               + " — rootCenterY=" + rootCenterY
               + " surfaceCenterY=" + surfaceCenterY
               + " deltaY=" + deltaY)
    }

    // ── Shell centering contract ──────────────────────────────────────────────
    // Login and Register pages must remain centered within their direct host
    // container (the Item wrapper inside each Component in AuthShell.qml).
    // Tolerance: <= 1px to account for sub-pixel rounding.

    function _assertCentered(page, label) {
        verify(page !== null, label + " found for centering check")
        var parent = page.parent
        verify(parent !== null, label + " has a parent container")
        var cx = parent.width / 2
        var cy = parent.height / 2
        var pcx = page.x + page.width / 2
        var pcy = page.y + page.height / 2
        verify(Math.abs(pcx - cx) <= 1,
               label + " horizontally centered in parent (parentCx=" + cx
               + " pageCx=" + pcx + " delta=" + Math.abs(pcx - cx) + ")")
        verify(Math.abs(pcy - cy) <= 1,
               label + " vertically centered in parent (parentCy=" + cy
               + " pageCy=" + pcy + " delta=" + Math.abs(pcy - cy) + ")")
    }

    function test_login_page_centered_in_host_container() {
        var shell = createPage("shells/AuthShell.qml")
        wait(200)
        var loginPage = findChildByName(shell, "authLoginPage")
        _assertCentered(loginPage, "LoginPage")
    }

    function test_register_page_centered_in_host_container() {
        var shell = createPage("shells/AuthShell.qml")
        shell.authMode = "register"
        wait(200)
        var regPage = findChildByName(shell, "authRegisterPage")
        _assertCentered(regPage, "RegisterPage")
    }
}
