import QtQuick 2.15
import QtTest 1.15

TestCase {
    name: "DesktopShareBrowsePage"

    function readQmlSource(relPath) {
        var xhr = new XMLHttpRequest()
        xhr.open("GET", Qt.resolvedUrl("../../../qml/" + relPath), false)
        xhr.send()
        verify(xhr.responseText.length > 0, relPath + " was read")
        return xhr.responseText
    }

    function readShareBrowseSource() {
        return readQmlSource("pages/ShareBrowsePage.qml")
    }

    function test_share_browse_chooses_save_destination() {
        var source = readShareBrowseSource()

        verify(source.indexOf('property string targetSaveFolderId: "0"') !== -1,
               "Keeps selected save target folder id")
        verify(source.indexOf('id: saveDestinationDialog') !== -1,
               "Has save destination dialog")
        verify(source.indexOf('objectName: "shareSaveDestinationDialog"') !== -1,
               "Save destination dialog has object name")
        verify(source.indexOf('FolderTreePanel') !== -1,
               "Uses existing folder tree picker")
        verify(source.indexOf('model: driveManager.treeModel') !== -1,
               "Folder picker uses drive manager tree model")
        verify(source.indexOf('driveManager.loadFolderTree()') !== -1,
               "Loads folder tree before choosing destination")
        verify(source.indexOf('onClicked: root.openSaveDestinationDialog()') !== -1,
               "Save button opens destination dialog")
        verify(source.indexOf('root.targetSaveFolderId') !== -1,
               "Saves to selected target folder")
        verify(source.indexOf('shareManager.saveShareItems(root.shareId, ids.fileIds, ids.folderIds, "0")') === -1,
               "Does not hard-code root destination")
    }
}
