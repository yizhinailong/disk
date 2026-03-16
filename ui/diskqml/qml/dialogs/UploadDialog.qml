/**
 * @file UploadDialog.qml
 * @brief 上传对话框 — 文件选择 + 目标文件夹选择
 *
 * @details
 * 提供简化的上传入口，支持选择目标文件夹。
 * 支持上传到：
 *   - 当前文件夹（默认）
 *   - 根目录
 *   - 自定义选择的文件夹（通过 FolderPickerDialog）
 *
 * 调用 TransfersViewModel.startUpload(fileUrls, targetFolderId)
 */
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import Disk 1.0

Item {
    id: root

    // ==================== 公共 API ====================
    
    /// 从当前文件夹打开上传对话框
    function openUpload() {
        destinationMode = "current"
        selectedFolderId = -1
        fileDialog.open()
    }

    /// 打开上传到根目录的对话框
    function openUploadToRoot() {
        destinationMode = "root"
        selectedFolderId = 0
        fileDialog.open()
    }

    // ==================== 内部状态 ====================

    /// 目标模式："当前"、"根目录" 或 "自定义"
    property string destinationMode: "current"
    /// 自定义选择的文件夹 ID（当模式为 "自定义" 时）
    property int selectedFolderId: -1

    // ==================== 文件对话框 ====================

    FileDialog {
        id: fileDialog
        title: qsTr("选择要上传的文件")
        fileMode: FileDialog.OpenFiles
        nameFilters: ["所有文件 (*)"]


        onAccepted: {
            // 根据模式确定目标文件夹 ID
            var targetFolderId = 0  // 默认为根目录

            if (destinationMode === "current") {
                targetFolderId = FileListViewModel.currentFolderId
            } else if (destinationMode === "root") {
                targetFolderId = 0
            } else if (destinationMode === "custom") {
                targetFolderId = selectedFolderId
            }

            // 无效目标保护：回退到根目录
            if (targetFolderId < 0) {
                console.warn("[UploadDialog] Invalid targetFolderId:", targetFolderId, "- falling back to root")
                targetFolderId = 0
            }

            // 将选中的文件转换为列表
            var fileUrls = []
            for (var i = 0; i < selectedFiles.length; i++) {
                fileUrls.push(selectedFiles[i])
            }

            // 通过 ViewModel 开始上传
            if (fileUrls.length > 0) {
                TransfersViewModel.startUpload(fileUrls, targetFolderId)
                console.log("[UploadDialog] Started upload of", fileUrls.length, "file(s) to folder", targetFolderId)
            }
        }
    }

    // ==================== 自定义目标的文件夹选择器 ====================

    FolderPickerDialog {
        id: folderPickerDialog
        folderTreeModel: FileListViewModel.folderTreeModel

        property var pendingFileUrls: []

        function openForUpload(fileUrls) {
            pendingFileUrls = fileUrls
            mode = "custom"
            selectedFolderId = -1
            open()
            FileListViewModel.loadFolderTree()
        }

        onAccepted: {
            if (selectedFolderId >= 0 && pendingFileUrls.length > 0) {
                TransfersViewModel.startUpload(pendingFileUrls, selectedFolderId)
                console.log("[UploadDialog] Started upload to custom folder:", selectedFolderId)
            }
            pendingFileUrls = []
        }

        onRejected: {
            pendingFileUrls = []
        }
    }

    // ==================== 目标选择对话框 ====================

    Dialog {
        id: destinationDialog
        title: qsTr("选择上传目标")
        modal: true
        width: 400
        anchors.centerIn: parent
        standardButtons: Dialog.Cancel

        property var pendingFileUrls: []

        function openWithFiles(fileUrls) {
            pendingFileUrls = fileUrls
            destinationMode = "current"
            open()
        }

        ColumnLayout {
            anchors.fill: parent
            spacing: 16

            Label {
                text: qsTr("请选择上传目标文件夹：")
                font.pixelSize: 14
                Layout.fillWidth: true
                wrapMode: Text.Wrap
            }

            // 当前文件夹选项
            RadioButton {
                id: currentFolderRadio
                text: qsTr("当前文件夹")
                checked: destinationDialog.destinationMode === "current"
                onCheckedChanged: if (checked) destinationDialog.destinationMode = "current"

                Layout.fillWidth: true
            }

            Label {
                text: qsTr("上传到: ") + (FileListViewModel.currentPath || "/")
                font.pixelSize: 11
                color: palette.placeholderText
                Layout.leftMargin: 32
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                visible: currentFolderRadio.checked
            }

            // 根目录选项
            RadioButton {
                id: rootFolderRadio
                text: qsTr("根目录")
                checked: destinationDialog.destinationMode === "root"
                onCheckedChanged: if (checked) destinationDialog.destinationMode = "root"

                Layout.fillWidth: true
            }

            // 自定义文件夹选项
            RadioButton {
                id: customFolderRadio
                text: qsTr("选择其他文件夹...")
                checked: destinationDialog.destinationMode === "custom"
                onCheckedChanged: if (checked) destinationDialog.destinationMode = "custom"

                Layout.fillWidth: true
            }

            Label {
                id: customFolderLabel
                text: root.selectedFolderId >= 0
                      ? qsTr("已选择文件夹 ID: ") + root.selectedFolderId
                      : qsTr("点击下方按钮选择文件夹")
                font.pixelSize: 11
                color: palette.placeholderText
                Layout.leftMargin: 32
                Layout.fillWidth: true
                visible: customFolderRadio.checked
            }

            Button {
                text: qsTr("浏览...")
                visible: customFolderRadio.checked
                Layout.leftMargin: 32
                onClicked: {
                    // 内联打开文件夹选择器
                    FileListViewModel.loadFolderTree()
                    inlineFolderPicker.visible = true
                }
            }

            // 内联文件夹选择器（简化的 TreeView）
            Rectangle {
                id: inlineFolderPicker
                visible: false
                Layout.fillWidth: true
                Layout.preferredHeight: 200
                color: palette.base
                border.color: palette.mid
                radius: 4

                property int pickedFolderId: -1

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 8
                    spacing: 8

                    RowLayout {
                        Layout.fillWidth: true

                        Label {
                            text: qsTr("选择目标文件夹:")
                            font.pixelSize: 12
                            font.bold: true
                        }

                        Item { Layout.fillWidth: true }

                        Button {
                            text: qsTr("关闭")
                            flat: true
                            onClicked: inlineFolderPicker.visible = false
                        }
                    }

                    // 根目录选项
                    ItemDelegate {
                        Layout.fillWidth: true
                        height: 32
                        highlighted: inlineFolderPicker.pickedFolderId === 0

                        contentItem: RowLayout {
                            spacing: 6
                            Label { text: "📁"; font.pixelSize: 14 }
                            Label { text: qsTr("根目录"); font.pixelSize: 12; Layout.fillWidth: true }
                        }

                        onClicked: {
                            inlineFolderPicker.pickedFolderId = 0
                            root.selectedFolderId = 0
                            customFolderLabel.text = qsTr("已选择: 根目录")
                            inlineFolderPicker.visible = false
                        }

                        background: Rectangle {
                            radius: 4
                            color: inlineFolderPicker.pickedFolderId === 0
                                   ? palette.highlight
                                   : parent.hovered ? palette.midlight : "transparent"
                        }
                    }

                    // 用于文件夹层级的 TreeView
                    ScrollView {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true

                        TreeView {
                            id: folderTreeView
                            anchors.fill: parent
                            model: FileListViewModel.folderTreeModel
                            visible: FileListViewModel.folderTreeModel ? !FileListViewModel.folderTreeModel.loading : false

                            delegate: TreeViewDelegate {
                                implicitHeight: 32
                                implicitWidth: folderTreeView.width

                                contentItem: RowLayout {
                                    spacing: 6
                                    Label {
                                        text: folderTreeView.isExpanded(row) ? "📂" : "📁"
                                        font.pixelSize: 12
                                    }
                                    Label {
                                        text: model.folderName ?? model.display ?? ""
                                        font.pixelSize: 12
                                        font.bold: inlineFolderPicker.pickedFolderId === (model.folderId ?? -1)
                                        Layout.fillWidth: true
                                        elide: Text.ElideRight
                                    }
                                }

                                background: Rectangle {
                                    radius: 4
                                    color: {
                                        var fid = model.folderId ?? -1
                                        if (inlineFolderPicker.pickedFolderId === fid) return palette.highlight
                                        if (hovered) return palette.midlight
                                        return "transparent"
                                    }
                                }

                                onClicked: {
                                    var fid = model.folderId ?? -1
                                    inlineFolderPicker.pickedFolderId = fid
                                    root.selectedFolderId = fid
                                    customFolderLabel.text = qsTr("已选择: ") + (model.folderName ?? "")
                                    inlineFolderPicker.visible = false
                                }
                            }
                        }
                    }

                    BusyIndicator {
                        anchors.centerIn: parent
                        running: FileListViewModel.folderTreeModel ? FileListViewModel.folderTreeModel.loading : false
                        visible: running
                    }
                }
            }

            Item { Layout.preferredHeight: 8 }

            // 上传按钮
            Button {
                text: qsTr("开始上传")
                highlighted: true
                enabled: destinationDialog.pendingFileUrls.length > 0
                          && (destinationDialog.destinationMode !== "custom" || root.selectedFolderId >= 0)
                Layout.alignment: Qt.AlignRight

                onClicked: {
                    var targetFolderId = 0

                    if (destinationDialog.destinationMode === "current") {
                        targetFolderId = FileListViewModel.currentFolderId
                    } else if (destinationDialog.destinationMode === "root") {
                        targetFolderId = 0
                    } else if (destinationDialog.destinationMode === "custom") {
                        targetFolderId = root.selectedFolderId
                    }

                    // 无效目标保护
                    if (targetFolderId < 0) {
                        console.warn("[UploadDialog] Invalid targetFolderId:", targetFolderId, "- falling back to root")
                        targetFolderId = 0
                    }

                    if (destinationDialog.pendingFileUrls.length > 0) {
                        TransfersViewModel.startUpload(destinationDialog.pendingFileUrls, targetFolderId)
                        console.log("[UploadDialog] Started upload of", destinationDialog.pendingFileUrls.length, "file(s) to folder", targetFolderId)
                    }

                    destinationDialog.pendingFileUrls = []
                    destinationDialog.close()
                }
            }
        }

        onRejected: {
            pendingFileUrls = []
        }
    }
}
