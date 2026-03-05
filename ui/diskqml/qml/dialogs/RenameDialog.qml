/**
 * @file RenameDialog.qml
 * @brief 重命名输入对话框 — 450px, 8px radius, pre-filled input + confirm/cancel
 */
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Disk 1.0

Dialog {
    id: dlg
    title: "✏️ 重命名"
    modal: true
    width: 450
    anchors.centerIn: parent
    standardButtons: Dialog.NoButton
    padding: 24

    /// File ID to rename.
    property int targetFileId: 0
    /// Current file name (pre-filled).
    property string currentName: ""

    function openForFile(fileId: int, fileName: string) {
        targetFileId = fileId
        currentName = fileName
        nameField.text = fileName
        dlg.open()
        nameField.forceActiveFocus()
        nameField.selectAll()
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 16

        Label {
            text: "新名称"
            font.pixelSize: 13
        }

        TextField {
            id: nameField
            Layout.fillWidth: true
            placeholderText: "请输入新名称"
            font.pixelSize: 14
            selectByMouse: true

            Keys.onReturnPressed: {
                if (nameField.text.trim().length > 0 && nameField.text.trim() !== dlg.currentName)
                    dlg.accept()
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
                text: "确认"
                highlighted: true
                enabled: nameField.text.trim().length > 0
                         && nameField.text.trim() !== dlg.currentName
                onClicked: dlg.accept()
            }
        }
    }

    onAccepted: {
        var newName = nameField.text.trim()
        if (newName.length > 0 && newName !== currentName) {
            FileListViewModel.renameFile(targetFileId, newName)
        }
    }
}
