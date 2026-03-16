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
    padding: StyleTokens.spacingLg

    background: Rectangle {
        color: StyleTokens.colorSurface
        radius: StyleTokens.radiusXl
        border.color: StyleTokens.colorBorder
        border.width: 1
    }

    Overlay.modal: Rectangle {
        color: Qt.rgba(0, 0, 0, 0.45)
    }

    ///< 操作模式（"移动" 或 "复制" 或 "custom"）
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
        spacing: StyleTokens.spacingMd

        // --- 标题 ---
        Label {
            text: {
                if (dlg.mode === "custom") {
                    return "请选择目标文件夹："
                }
                var op = dlg.mode === "move" ? "移动" : "复制"
                if (dlg.fileIds.length === 1) {
                    return op + " \"" + dlg.displayName + "\" 到："
                }
                return op + " " + dlg.fileIds.length + " 个项目到："
            }
            font.pixelSize: StyleTokens.fontSizeBody
            font.weight: StyleTokens.fontWeightBody
            color: StyleTokens.colorTextPrimary
            wrapMode: Text.Wrap
            Layout.fillWidth: true
        }

        // --- 根目录项 ---
        ItemDelegate {
            Layout.fillWidth: true
            height: 36
            highlighted: dlg.selectedFolderId === 0

            contentItem: RowLayout {
                spacing: StyleTokens.spacingSm
                Label {
                    text: "📁"
                    font.pixelSize: StyleTokens.fontSizeBody
                }
                Label {
                    text: "根目录"
                    font.pixelSize: StyleTokens.fontSizeBody
                    font.weight: dlg.selectedFolderId === 0 ? StyleTokens.fontWeightH3 : StyleTokens.fontWeightBody
                    color: dlg.selectedFolderId === 0 ? StyleTokens.colorPrimary : StyleTokens.colorTextPrimary
                    Layout.fillWidth: true
                }
            }

            onClicked: dlg.selectedFolderId = 0

            background: Rectangle {
                radius: StyleTokens.radiusSmall
                color: dlg.selectedFolderId === 0
                       ? StyleTokens.colorPrimaryLight
                       : parent.hovered ? StyleTokens.colorHover : "transparent"
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
                        spacing: StyleTokens.spacingSm

                        // 缩进由 TreeViewDelegate 自动处理

                        Label {
                            text: treeView.isExpanded(row) ? "📂" : "📁"
                            font.pixelSize: StyleTokens.fontSizeBody
                        }

                        Label {
                            text: model.folderName ?? model.display ?? ""
                            font.pixelSize: StyleTokens.fontSizeBody
                            font.weight: dlg.selectedFolderId === (model.folderId ?? -1) ? StyleTokens.fontWeightH3 : StyleTokens.fontWeightBody
                            color: dlg.selectedFolderId === (model.folderId ?? -1) ? StyleTokens.colorPrimary : StyleTokens.colorTextPrimary
                            Layout.fillWidth: true
                            elide: Text.ElideRight
                        }
                    }

                    background: Rectangle {
                        radius: StyleTokens.radiusSmall
                        color: {
                            var fid = model.folderId ?? -1
                            if (dlg.selectedFolderId === fid) return StyleTokens.colorPrimaryLight
                            if (treeDelegate.hovered) return StyleTokens.colorHover
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
            spacing: StyleTokens.spacingSm

            Item { Layout.fillWidth: true }

            Button {
                text: "取消"
                onClicked: dlg.reject()
                
                background: Rectangle {
                    implicitHeight: 36
                    implicitWidth: 80
                    color: parent.down ? StyleTokens.colorHover : "transparent"
                    border.color: StyleTokens.colorBorder
                    border.width: 1
                    radius: StyleTokens.radiusSmall
                }
                contentItem: Text {
                    text: parent.text
                    font.pixelSize: StyleTokens.fontSizeBody
                    color: StyleTokens.colorTextPrimary
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
            }

            Button {
                text: dlg.mode === "move" ? "移动到此" : (dlg.mode === "copy" ? "复制到此" : "选择")
                enabled: dlg.selectedFolderId >= 0
                onClicked: dlg.accept()
                
                background: Rectangle {
                    implicitHeight: 36
                    implicitWidth: 80
                    color: !parent.enabled ? StyleTokens.colorBackground : (parent.down ? StyleTokens.colorPrimaryHover : (parent.hovered ? Qt.lighter(StyleTokens.colorPrimary, 1.1) : StyleTokens.colorPrimary))
                    radius: StyleTokens.radiusSmall
                }
                contentItem: Text {
                    text: parent.text
                    font.pixelSize: StyleTokens.fontSizeBody
                    color: !parent.enabled ? StyleTokens.colorTextTertiary : "#FFFFFF"
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
            }
        }
    }

    onAccepted: {
        if (dlg.selectedFolderId < 0) return

        if (dlg.mode === "move") {
            FileListViewModel.moveFiles(dlg.fileIds, dlg.selectedFolderId)
        } else if (dlg.mode === "copy") {
            FileListViewModel.copyFiles(dlg.fileIds, dlg.selectedFolderId)
        }
    }
}
