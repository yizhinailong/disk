import QtQuick 2.15
import QtTest 1.15
import QuickTestSupport 1.0

TestCase {
    id: testRoot
    name: "DesktopFolderTreePanel"
    when: windowShown

    width: 420
    height: 420

    property var _created: []

    FolderTreeTestHarness {
        id: harness
    }

    SignalSpy {
        id: folderClickedSpy
        signalName: "folderClicked"
    }

    function _tryInjectStubs() {
        var s = null
        try { s = _q_quicktest_setup } catch (e) {}
        if (!s) {
            try { s = setup } catch (e2) {}
        }
        if (s && typeof s.inject === "function") {
            s.inject()
        }
    }

    function treeHarness() {
        return harness
    }

    function initTestCase() {
        _tryInjectStubs()
    }

    function cleanup() {
        folderClickedSpy.target = null
        folderClickedSpy.clear()

        for (var i = 0; i < _created.length; ++i) {
            if (_created[i]) {
                _created[i].destroy()
            }
        }

        _created = []
        var harness = treeHarness()
        if (harness) {
            harness.LoadNavigationTree()
        }
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

        return prefix + stack.join("/")
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

    function readFolderTreePanelSource() {
        var xhr = new XMLHttpRequest()
        xhr.open("GET", Qt.resolvedUrl("../../../qml/components/FolderTreePanel.qml"), false)
        xhr.send()
        verify(xhr.responseText.length > 0, "FolderTreePanel.qml was read")
        return xhr.responseText
    }

    function createPanel(properties) {
        var component = loadComponent("components/FolderTreePanel.qml")
        var panel = component.createObject(testRoot, properties || {})
        verify(panel !== null, "Panel instance created")
        registerObject(panel)
        panel.visible = true
        folderClickedSpy.target = panel
        folderClickedSpy.clear()
        wait(50)
        return panel
    }

    function findByObjectName(item, objectName) {
        if (!item) {
            return null
        }
        if (item.objectName === objectName) {
            return item
        }

        if (item.item !== undefined && item.item !== null && typeof item.item === "object") {
            var found = findByObjectName(item.item, objectName)
            if (found) {
                return found
            }
        }

        if (item.contentItem !== undefined && item.contentItem !== null && item.contentItem !== item) {
            found = findByObjectName(item.contentItem, objectName)
            if (found) {
                return found
            }
        }

        if (item.data !== undefined && item.data !== null) {
            for (var dataIndex = 0; dataIndex < item.data.length; ++dataIndex) {
                found = findByObjectName(item.data[dataIndex], objectName)
                if (found) {
                    return found
                }
            }
        }

        if (item.children) {
            for (var childIndex = 0; childIndex < item.children.length; ++childIndex) {
                found = findByObjectName(item.children[childIndex], objectName)
                if (found) {
                    return found
                }
            }
        }

        return null
    }

    function waitForObject(item, objectName) {
        var found = null
        tryVerify(function() {
            found = findByObjectName(item, objectName)
            return found !== null
        }, 1000, objectName + " becomes available")
        return found
    }

    function clickCenter(item) {
        mouseClick(item, item.width / 2, item.height / 2, Qt.LeftButton)
        wait(10)
    }

    function moveMouseTo(item) {
        mouseMove(item, item.width / 2, item.height / 2)
        wait(10)
    }

    function test_tree_expands_to_active_folder_path() {
        var harness = treeHarness()
        verify(harness !== null, "Folder tree harness is available")
        harness.LoadNavigationTree()
        var panel = createPanel({
            width: 260,
            height: 320,
            model: harness.treeModel,
            currentFolderId: "30"
        })

        var treeView = waitForObject(panel, "folderTreeView")
        var documentsRow = waitForObject(panel, "folderRow_10")
        var projectRow = waitForObject(panel, "folderRow_20")
        var targetRow = waitForObject(panel, "folderRow_30")

        verify(treeView.isExpanded(documentsRow.row), "Documents branch expanded to reveal active path")
        verify(treeView.isExpanded(projectRow.row), "Project branch expanded to reveal active path")
        compare(targetRow.activeFolder, true, "Nested active folder is marked active")
    }

    function test_disclosure_toggles_expand_without_activation() {
        var harness = treeHarness()
        verify(harness !== null, "Folder tree harness is available")
        harness.LoadNavigationTree()
        var panel = createPanel({
            width: 260,
            height: 320,
            model: harness.treeModel
        })

        var treeView = waitForObject(panel, "folderTreeView")
        var documentsRow = waitForObject(panel, "folderRow_10")
        verify(!treeView.isExpanded(documentsRow.row), "Documents starts collapsed")
        treeView.toggleExpanded(documentsRow.row)
        wait(10)
        verify(treeView.isExpanded(documentsRow.row), "Disclosure expands the branch")
        compare(folderClickedSpy.count, 0, "Expand gesture does not activate the folder")

        treeView.toggleExpanded(documentsRow.row)
        wait(10)
        verify(!treeView.isExpanded(documentsRow.row), "Disclosure collapses the branch")
        compare(folderClickedSpy.count, 0, "Collapse gesture also avoids folder activation")
    }

    function test_row_activation_emits_folder_clicked_once() {
        var harness = treeHarness()
        verify(harness !== null, "Folder tree harness is available")
        harness.LoadNavigationTree()
        var panel = createPanel({
            width: 260,
            height: 320,
            model: harness.treeModel
        })

        var treeView = waitForObject(panel, "folderTreeView")
        var archiveRow = waitForObject(panel, "folderRow_50")
        panel.activateFolder("50", treeView.index(archiveRow.row, 0))

        compare(folderClickedSpy.count, 1, "One click produces one activation signal")
        compare(folderClickedSpy.signalArguments[0][0], "50", "Activation reports the clicked folder id")
        compare(archiveRow.current, true, "Clicked row becomes the current row")
    }

    function test_active_current_hover_and_idle_rows_stay_distinct() {
        var harness = treeHarness()
        verify(harness !== null, "Folder tree harness is available")
        harness.LoadNavigationTree()
        var panel = createPanel({
            width: 280,
            height: 320,
            model: harness.treeModel,
            currentFolderId: "40"
        })

        var treeView = waitForObject(panel, "folderTreeView")
        var activeRow = waitForObject(panel, "folderRow_40")
        var currentRow = waitForObject(panel, "folderRow_50")
        var hoverRow = waitForObject(panel, "folderRow_60")
        var idleRow = waitForObject(panel, "folderRow_10")

        panel.activateFolder("50", treeView.index(currentRow.row, 0))
        compare(folderClickedSpy.count, 1, "Changing current row still emits a single activation")
        compare(activeRow.activeFolder, true, "Bound currentFolderId keeps the active row pinned")
        compare(currentRow.current, true, "Clicked row becomes current without taking active styling")

        verify(activeRow.rowFillColor !== currentRow.rowFillColor, "Active row fill differs from current row fill")
        verify(currentRow.rowFillColor !== idleRow.rowFillColor, "Current row fill differs from idle row fill")
        verify(panel.hoverRowColor !== idleRow.rowFillColor, "Hover row fill differs from idle row fill")
        verify(panel.hoverRowColor !== activeRow.rowFillColor, "Hover row fill differs from active row fill")
        verify(panel.hoverRowColor !== currentRow.rowFillColor, "Hover row fill differs from current row fill")
        verify(hoverRow.rowTextColor !== activeRow.rowTextColor, "Ancestor and active text treatments stay distinct")
    }

    function test_long_names_elide_without_overlapping_disclosure() {
        var harness = treeHarness()
        verify(harness !== null, "Folder tree harness is available")
        harness.LoadLongNameTree()
        var panel = createPanel({
            width: 220,
            height: 260,
            model: harness.treeModel
        })

        var longNameRow = waitForObject(panel, "folderRow_100")
        waitForObject(panel, "folderDisclosure_100")

        compare(longNameRow.hasChildren, true, "Long-name row keeps a disclosure control")
        compare(longNameRow.labelTruncated, true, "Long names deterministically elide in the available width")
        compare(longNameRow.labelClearsDisclosure, true, "Elided label does not overlap the disclosure control")
    }

    function test_long_names_keep_hover_disclosure_contract() {
        var source = readFolderTreePanelSource()

        verify(source.indexOf('readonly property bool showNameToolTip: labelTruncated && rowMouseArea.containsMouse') !== -1,
               "Tree rows only expose hover disclosure when the label is truly truncated")
        verify(source.indexOf('ToolTip.visible: treeRow.showNameToolTip') !== -1,
               "Tree rows bind tooltip visibility to the deterministic hover contract")
        verify(source.indexOf('ToolTip.text: treeRow.folderName') !== -1,
               "Tree row disclosure reveals the full folder name")
        verify(source.indexOf('width: Math.max(0, parent.width - x - root.rowHorizontalPadding)') !== -1,
               "Tree labels clamp their available width to avoid disclosure collisions")
    }

    function test_empty_model_does_not_crash() {
        var harness = treeHarness()
        verify(harness !== null, "Folder tree harness is available")

        harness.LoadEmptyTree()
        var panel = createPanel({
            width: 260,
            height: 320,
            model: harness.treeModel
        })

        var treeView = findByObjectName(panel, "folderTreeView")
        verify(treeView !== null, "TreeView exists with empty model")
        compare(harness.treeModel.rowCount(), 0,
                "Tree model reports zero rows when empty")

        panel.currentFolderId = "999"
        wait(50)
        compare(panel.currentFolderId, "999",
                "Panel survives currentFolderId assignment on empty tree")

        panel.currentFolderId = ""
        wait(50)
        compare(panel.currentFolderId, "",
                "Panel survives clearing currentFolderId on empty tree")
    }

    function test_panel_survives_model_reload() {
        var harness = treeHarness()
        verify(harness !== null, "Folder tree harness is available")
        harness.LoadNavigationTree()
        var panel = createPanel({
            width: 260,
            height: 320,
            model: harness.treeModel,
            currentFolderId: "30"
        })

        var targetRow = waitForObject(panel, "folderRow_30")
        verify(targetRow !== null, "Active row exists before reload")

        harness.LoadEmptyTree()
        wait(50)
        compare(harness.treeModel.rowCount(), 0,
                "Tree is empty after clear reload")

        harness.LoadNavigationTree()
        wait(50)
        compare(harness.treeModel.rowCount(), 3,
                "Tree repopulated after reload")

        var restoredRow = waitForObject(panel, "folderRow_30")
        verify(restoredRow !== null,
               "Active row restored after tree reload")
        compare(restoredRow.activeFolder, true,
                "Active state restored on previously active folder")
    }

    function test_sync_with_missing_folder_is_safe() {
        var harness = treeHarness()
        verify(harness !== null, "Folder tree harness is available")
        harness.LoadNavigationTree()
        var panel = createPanel({
            width: 260,
            height: 320,
            model: harness.treeModel,
            currentFolderId: "9999"
        })

        wait(100)

        compare(panel.currentFolderId, "9999",
                "Panel keeps the unknown currentFolderId")

        var treeView = findByObjectName(panel, "folderTreeView")
        verify(treeView !== null, "TreeView is alive despite unknown folder id")

        panel.currentFolderId = "10"
        wait(50)

        var docsRow = waitForObject(panel, "folderRow_10")
        compare(docsRow.activeFolder, true,
                "Switching to a known folder restores active highlight")
    }
}
