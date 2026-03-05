/**
 * @file FolderPickerDialog.qml
 * @brief 文件夹选择对话框 — TreeView for selecting move/copy destination
 *
 * @details
 * Uses a TreeView to display the full folder hierarchy.
 * The dialog receives a FolderTreeModel from C++ (populated by FileListViewModel).
 * Returns the selected folder ID on accept.
 */
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Disk 1.0

Dialog {
    id: dlg
    title: "📂 选择目标文件夹"
    modal: true
    width: 500
    height: 450
    anchors.centerIn: parent
    standardButtons: Dialog.NoButton
    padding: 24

    /// Operation mode: "move" or "copy"
    property string mode: "move"
    /// List of file IDs to move/copy.
    property var fileIds: []
    /// Display name for single-item operations.
    property string displayName: ""
    /// Currently selected folder ID (-1 = none selected).
    property int selectedFolderId: -1
    /// The tree model instance (set from parent).
    property var folderTreeModel: null

    function openForMove(ids: var, name: string) {
        mode = "move"
        fileIds = ids
        displayName = name
        selectedFolderId = -1
        dlg.open()
        // Trigger tree load via ViewModel
        FileListViewModel.loadFolderTree()
    }

    function openForCopy(ids: var, name: string) {
        mode = "copy"
        fileIds = ids
        displayName = name
        selectedFolderId = -1
        dlg.open()
        // Trigger tree load via ViewModel
        FileListViewModel.loadFolderTree()
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 12

        // --- Header ---
        Label {
            text: {
                var op = dlg.mode === "move" ? "移动" : "复制"
                if (dlg.fileIds.length === 1) {
                    return op + " \"" + dlg.displayName + "\" 到："
                }
                return op + " " + dlg.fileIds.length + " 个项目到："
            }
            font.pixelSize: 14
            wrapMode: Text.Wrap
            Layout.fillWidth: true
        }

        // --- Root item (根目录) ---
        ItemDelegate {
            Layout.fillWidth: true
            height: 36
            highlighted: dlg.selectedFolderId === 0

            contentItem: RowLayout {
                spacing: 6
                Label {
                    text: "📁"
                    font.pixelSize: 14
                }
                Label {
                    text: "根目录"
                    font.pixelSize: 13
                    font.bold: dlg.selectedFolderId === 0
                    Layout.fillWidth: true
                }
            }

            onClicked: dlg.selectedFolderId = 0

            background: Rectangle {
                radius: 4
                color: dlg.selectedFolderId === 0
                       ? palette.highlight
                       : parent.hovered ? palette.midlight : "transparent"
            }
        }

        // --- Tree content ---
        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true

            // Loading indicator
            BusyIndicator {
                anchors.centerIn: parent
                running: dlg.folderTreeModel ? dlg.folderTreeModel.loading : false
                visible: running
            }

            TreeView {
                id: treeView
                anchors.fill: parent
                model: dlg.folderTreeModel
                visible: dlg.folderTreeModel ? !dlg.folderTreeModel.loading : false

                delegate: TreeViewDelegate {
                    id: treeDelegate
                    implicitHeight: 36
                    implicitWidth: treeView.width

                    contentItem: RowLayout {
                        spacing: 6

                        // Indent is handled by TreeViewDelegate automatically

                        Label {
                            text: treeView.isExpanded(row) ? "📂" : "📁"
                            font.pixelSize: 14
                        }

                        Label {
                            text: model.folderName ?? model.display ?? ""
                            font.pixelSize: 13
                            font.bold: dlg.selectedFolderId === (model.folderId ?? -1)
                            Layout.fillWidth: true
                            elide: Text.ElideRight
                        }
                    }

                    background: Rectangle {
                        radius: 4
                        color: {
                            var fid = model.folderId ?? -1
                            if (dlg.selectedFolderId === fid) return palette.highlight
                            if (treeDelegate.hovered) return palette.midlight
                            return "transparent"
                        }
                    }

                    onClicked: {
                        var fid = model.folderId ?? -1
                        dlg.selectedFolderId = fid
                    }
                }
            }
        }

        // --- Button row ---
        RowLayout {
            Layout.fillWidth: true
            spacing: 12

            Item { Layout.fillWidth: true }

            Button {
                text: "取消"
                onClicked: dlg.reject()
            }

            Button {
                text: dlg.mode === "move" ? "移动到此" : "复制到此"
                highlighted: true
                enabled: dlg.selectedFolderId >= 0
                onClicked: dlg.accept()
            }
        }
    }

    onAccepted: {
        if (dlg.selectedFolderId < 0) return

        if (dlg.mode === "move") {
            FileListViewModel.moveFiles(dlg.fileIds, dlg.selectedFolderId)
        } else {
            FileListViewModel.copyFiles(dlg.fileIds, dlg.selectedFolderId)
        }
    }
}
