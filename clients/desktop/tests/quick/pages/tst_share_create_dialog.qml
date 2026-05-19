import QtQuick 2.15
import QtTest 1.15

TestCase {
    id: testRoot
    name: "DesktopShareCreateDialog"
    when: windowShown

    width: 1024
    height: 640

    property var _created: []

    function cleanup() {
        for (var i = 0; i < _created.length; ++i) {
            if (_created[i]) {
                _created[i].destroy()
            }
        }
        _created = []
    }

    function registerObject(obj) {
        _created.push(obj)
        return obj
    }

    function sourceUrl(relPath) {
        var base = Qt.resolvedUrl(".").toString()
        return normalizeFileUrl(base + "../../../qml/" + relPath)
    }

    function normalizeFileUrl(url) {
        var idx = url.indexOf("://")
        var sep = idx >= 0 ? idx + 3 : 0
        var prefix = url.substring(0, sep)
        var path = url.substring(sep)
        var parts = path.split("/")
        var stack = []

        for (var i = 0; i < parts.length; ++i) {
            if (parts[i] === "..") {
                if (stack.length > 0) {
                    stack.pop()
                }
            } else if (parts[i] !== "" && parts[i] !== ".") {
                stack.push(parts[i])
            }
        }

        var leadingSlash = path.charAt(0) === "/" ? "/" : ""
        return prefix + leadingSlash + stack.join("/")
    }

    function loadComponent(relPath) {
        var component = Qt.createComponent(sourceUrl(relPath))
        verify(component !== null, "Component created for " + relPath)
        if (component.status === Component.Loading) {
            wait(500)
        }
        compare(component.status, Component.Ready, component.errorString())
        return component
    }

    function createPage() {
        var component = loadComponent("pages/DriveBrowserPage.qml")
        var page = component.createObject(testRoot, {
            width: 1024,
            height: 640
        })
        verify(page !== null, "DriveBrowserPage instance created")
        registerObject(page)
        page.visible = true
        wait(100)
        return page
    }

    function findByObjectName(item, objectName) {
        if (!item) return null
        if (item.objectName === objectName) return item

        if (item.item !== undefined && item.item !== null && typeof item.item === "object") {
            var loaded = findByObjectName(item.item, objectName)
            if (loaded) return loaded
        }

        if (item.contentItem !== undefined && item.contentItem !== null && item.contentItem !== item) {
            var found = findByObjectName(item.contentItem, objectName)
            if (found) return found
        }

        if (item.data !== undefined && item.data !== null) {
            for (var i = 0; i < item.data.length; ++i) {
                found = findByObjectName(item.data[i], objectName)
                if (found) return found
            }
        }

        if (item.children) {
            for (var j = 0; j < item.children.length; ++j) {
                found = findByObjectName(item.children[j], objectName)
                if (found) return found
            }
        }

        return null
    }

    // ── Property defaults ────────────────────────────────────────────────

    function test_createDialogState_defaults_to_form() {
        var page = createPage()
        compare(page.createDialogState, "form",
                "createDialogState defaults to 'form'")
    }

    function test_createdShareLink_defaults_to_empty() {
        var page = createPage()
        compare(page.createdShareLink, "",
                "createdShareLink defaults to empty string")
    }

    function test_createdShareId_defaults_to_empty() {
        var page = createPage()
        compare(page.createdShareId, "",
                "createdShareId defaults to empty string")
    }

    // ── shareCreated signal transitions to success state ─────────────────

    function test_shareCreated_sets_state_to_success() {
        var page = createPage()
        var sm = shareManager
        verify(sm !== null, "shareManager available")

        compare(page.createDialogState, "form", "Starts in form state")

        sm.shareCreated("abc12345", "/s/abc12345")
        wait(50)

        compare(page.createDialogState, "success",
                "shareCreated transitions createDialogState to 'success'")
        compare(page.createdShareId, "abc12345",
                "shareCreated stores the share id")
        compare(page.createdShareLink, "/s/abc12345",
                "shareCreated stores the share link")
    }

    // ── Success state UI elements ────────────────────────────────────────

    function test_success_state_shows_share_link_label() {
        var page = createPage()
        var sm = shareManager
        verify(sm !== null, "shareManager available")

        page.openCreateShareDialog()
        wait(50)

        sm.shareCreated("xyz99999", "/s/xyz99999")
        wait(50)

        var linkLabel = findByObjectName(page, "shareSuccessLinkLabel")
        verify(linkLabel !== null,
               "shareSuccessLinkLabel exists in success state")
        compare(linkLabel.text, "/s/xyz99999",
                "Link label shows the created share link")
    }

    function test_success_state_shows_share_code_label() {
        var page = createPage()
        var sm = shareManager
        verify(sm !== null, "shareManager available")

        page.openCreateShareDialog()
        wait(50)

        sm.shareCreated("code8888", "/s/code8888")
        wait(50)

        var codeLabel = findByObjectName(page, "shareSuccessCodeLabel")
        verify(codeLabel !== null,
               "shareSuccessCodeLabel exists in success state")
        compare(codeLabel.text, "code8888",
                "Code label shows the created share id")
    }

    function test_success_state_shows_copy_button() {
        var page = createPage()
        var sm = shareManager
        verify(sm !== null, "shareManager available")

        page.openCreateShareDialog()
        wait(50)

        sm.shareCreated("cp999001", "/s/cp999001")
        wait(50)

        var copyCodeBtn = findByObjectName(page, "shareSuccessCopyCodeButton")
        verify(copyCodeBtn !== null,
               "shareSuccessCopyCodeButton exists in success state")
        compare(copyCodeBtn.text, "复制分享码",
                "Copy code button has correct label")

        var copyBtn = findByObjectName(page, "shareSuccessCopyButton")
        verify(copyBtn !== null,
               "shareSuccessCopyButton exists in success state")
        compare(copyBtn.text, "复制链接",
                "Copy button has correct label")
    }

    function test_success_state_shows_done_button() {
        var page = createPage()
        var sm = shareManager
        verify(sm !== null, "shareManager available")

        page.openCreateShareDialog()
        wait(50)

        sm.shareCreated("dn001122", "/s/dn001122")
        wait(50)

        var doneBtn = findByObjectName(page, "shareSuccessDoneButton")
        verify(doneBtn !== null,
               "shareSuccessDoneButton exists in success state")
        compare(doneBtn.text, "完成",
                "Done button has correct label")
    }

    // ── Done button resets state and closes dialog ───────────────────────

    function test_done_button_resets_state_and_closes_dialog() {
        var page = createPage()
        var sm = shareManager
        verify(sm !== null, "shareManager available")

        page.openCreateShareDialog()
        wait(50)

        var dialog = findByObjectName(page, "createShareDialog")
        verify(dialog !== null, "Create share dialog found")
        verify(dialog.opened, "Dialog is open after openCreateShareDialog")

        sm.shareCreated("rs556677", "/s/rs556677")
        wait(50)

        compare(page.createDialogState, "success",
                "State is success before clicking done")

        var doneBtn = findByObjectName(page, "shareSuccessDoneButton")
        verify(doneBtn !== null, "Done button found")
        doneBtn.clicked()
        wait(50)

        compare(page.createDialogState, "form",
                "Done button resets createDialogState to 'form'")
        compare(page.createdShareLink, "",
                "Done button clears createdShareLink")
        compare(page.createdShareId, "",
                "Done button clears createdShareId")
        verify(!dialog.opened,
               "Dialog is closed after clicking done")
    }

    // ── onClosed resets success state ────────────────────────────────────

    function test_dialog_close_resets_success_state() {
        var page = createPage()
        var sm = shareManager
        verify(sm !== null, "shareManager available")

        page.openCreateShareDialog()
        wait(50)

        sm.shareCreated("cl998877", "/s/cl998877")
        wait(50)

        compare(page.createDialogState, "success", "In success state")

        var dialog = findByObjectName(page, "createShareDialog")
        verify(dialog !== null, "Dialog found")
        dialog.close()
        wait(50)

        compare(page.createDialogState, "form",
                "Closing dialog resets state to form")
        compare(page.createdShareLink, "",
                "Closing dialog clears link")
        compare(page.createdShareId, "",
                "Closing dialog clears id")
    }

    // ── Form state UI is hidden in success state ─────────────────────────

    function test_form_controls_hidden_in_success_state() {
        var page = createPage()
        var sm = shareManager
        verify(sm !== null, "shareManager available")

        page.openCreateShareDialog()
        wait(50)

        var formContainer = findByObjectName(page, "shareFormContainer")
        verify(formContainer !== null, "Form container found in form state")
        verify(formContainer.visible, "Form container visible in form state")

        sm.shareCreated("fh112233", "/s/fh112233")
        wait(50)

        verify(!formContainer.visible,
               "Form container is hidden in success state")
    }

    function test_success_container_hidden_in_form_state() {
        var page = createPage()

        page.openCreateShareDialog()
        wait(50)

        var successContainer = findByObjectName(page, "shareSuccessContainer")
        verify(successContainer !== null, "Success container exists")
        verify(!successContainer.visible,
               "Success container is hidden in form state")
    }
}
