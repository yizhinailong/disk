import QtQuick 2.15
import QtTest 1.15

TestCase {
    id: testRoot
    name: "DesktopDriveSharedView"
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

    // ── Source contract helpers ──────────────────────────────────────────

    function readQmlSource(relPath) {
        var xhr = new XMLHttpRequest()
        xhr.open("GET", Qt.resolvedUrl("../../../qml/" + relPath), false)
        xhr.send()
        verify(xhr.responseText.length > 0, relPath + " was read")
        return xhr.responseText
    }

    function readSharedViewSource() {
        return readQmlSource("components/drive/DriveSharedView.qml")
    }

    // ── Source contract tests ────────────────────────────────────────────

    function test_shared_view_source_has_copy_link_button() {
        var source = readSharedViewSource()

        verify(source.indexOf("sharedCopyLinkButton_") !== -1,
               "Source contains a copy link button with objectName pattern sharedCopyLinkButton_")
    }

    function test_copy_button_references_clipboard() {
        var source = readSharedViewSource()

        verify(source.indexOf("clipboard") !== -1,
               "Source references clipboard for copy functionality")
        verify(source.indexOf("setText") !== -1,
               "Source calls setText on clipboard")
    }

    function test_copy_button_uses_share_link_model_data() {
        var source = readSharedViewSource()

        verify(source.indexOf("model.shareLink") !== -1,
               "Copy button uses model.shareLink as the data to copy")
    }

    function test_copy_button_has_visual_feedback() {
        var source = readSharedViewSource()

        // The feedback mechanism: a property that changes text/icon after click
        verify(source.indexOf("copyFeedbackActive") !== -1
               || source.indexOf("copyPressed") !== -1
               || source.indexOf("copied") !== -1,
               "Source has a feedback flag that changes after copy click")

        // Timer to reset feedback
        verify(source.indexOf("copyFeedbackTimer") !== -1
               || source.indexOf("copyTimer") !== -1,
               "Source has a timer to reset the copy feedback state")
    }

    function test_copy_button_has_feedback_check_mark() {
        var source = readSharedViewSource()

        verify(source.indexOf("\u2713") !== -1 || source.indexOf("check") !== -1,
               "Source contains a check mark or 'check' indicator for feedback state")
    }

    function test_copy_button_timer_resets_after_2_seconds() {
        var source = readSharedViewSource()

        // Find the interval value near the copy timer — should be ~2000ms
        var timerPattern = /interval:\s*2000/
        verify(timerPattern.test(source),
               "Copy feedback timer resets after 2000ms")
    }

    // ── Visitor entry section source contract tests ─────────────────────

    function test_shared_view_source_has_visitor_entry_section() {
        var source = readSharedViewSource()

        verify(source.indexOf("visitorEntrySection") !== -1,
               "Source contains a visitor entry section container")
    }

    function test_visitor_entry_has_text_field() {
        var source = readSharedViewSource()

        verify(source.indexOf("visitorShareInput") !== -1,
               "Source contains a TextField with objectName visitorShareInput")
    }

    function test_visitor_entry_has_access_button() {
        var source = readSharedViewSource()

        verify(source.indexOf("visitorAccessButton") !== -1,
               "Source contains a Button with objectName visitorAccessButton")
    }

    function test_visitor_entry_button_calls_parseShareInput() {
        var source = readSharedViewSource()

        verify(source.indexOf("parseShareInput") !== -1,
               "Source calls parseShareInput on shareManager")
    }

    function test_visitor_entry_button_calls_navigateToVisitor() {
        var source = readSharedViewSource()

        verify(source.indexOf("navigateToVisitor") !== -1,
               "Source calls navigateToVisitor on shellController")
    }

    function test_visitor_entry_has_error_label() {
        var source = readSharedViewSource()

        verify(source.indexOf("visitorEntryError") !== -1,
               "Source contains a visitor entry error property/label")
    }

    // ── Runtime tests ────────────────────────────────────────────────────

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

    function createDrivePage() {
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

    function test_copy_button_exists_at_runtime() {
        var page = createDrivePage()
        var sm = shareManager
        verify(sm !== null, "shareManager available")

        sm.clearShareListModel()
        sm.addShareItem("shr-copy1", "Report.pdf", "download", "active", true,
                        5, 2, "/s/abc12345", "2026-04-28T14:30:00Z", "2026-05-05T14:30:00Z", 1)

        page.activateViewMode("shared")
        sm.paginationLoaded(1, 1, 1)
        wait(100)

        var copyBtn = findByObjectName(page, "sharedCopyLinkButton_shr-copy1")
        verify(copyBtn !== null,
               "Copy link button exists at runtime with correct objectName")
    }

    function test_copy_button_displays_default_text() {
        var page = createDrivePage()
        var sm = shareManager
        verify(sm !== null, "shareManager available")

        sm.clearShareListModel()
        sm.addShareItem("shr-copy2", "Design.pdf", "view", "active", false,
                        0, 0, "/s/xyz99999")

        page.activateViewMode("shared")
        sm.paginationLoaded(1, 1, 1)
        wait(100)

        var copyBtn = findByObjectName(page, "sharedCopyLinkButton_shr-copy2")
        verify(copyBtn !== null, "Copy button found")
        verify(copyBtn.text.length > 0,
               "Copy button has visible text label")
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
}
