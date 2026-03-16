/**
 * @file FolderPickerDialog.qml
 * @brief 文件夹选择对话框 — 用于选择移动/复制目标的 TreeView
 *
 * @details
 * 使用 TreeView 显示完整的文件夹层级。
 * 对话框从 C++ 接收 FolderTreeModel（由 FileListViewModel 填充）。
 * 接受时返回选中的文件夹 ID。
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

    ///< 操作模式（"移动" 或 "复制"）
    property string mode: "move"
    ///< 待移动/复制的文件ID列表
    property var fileIds: []
    ///< 显示名称（单个文件操作时使用）
    property string displayName: ""
    ///< 当前选中的文件夹ID（-1 表示未选中）
    property int selectedFolderId: -1
    ///< 文件夹树模型（由父组件设置）
    property var folderTreeModel: null

    function openForMove(ids: var, name: string) {
        mode = "move"
        fileIds = ids
        displayName = name
        selectedFolderId = -1
        dlg.open()
        // 触发通过 ViewModel 加载文件夹树
        FileListViewModel.loadFolderTree()
    }

    function openForCopy(ids: var, name: string) {
        mode = "copy"
        fileIds = ids
        displayName = name
        selectedFolderId = -1
        dlg.open()
        // 触发通过 ViewModel 加载文件夹树
        FileListViewModel.loadFolderTree()
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 12

        // --- 标题 ---
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

        // --- 根目录项 ---
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

        // --- 树形内容 ---
        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true

            // 加载指示器
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

                        // 缩进由 TreeViewDelegate 自动处理

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

        // --- 按钮行 ---
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
