/**
 * @file NewFolderDialog.qml
 * @brief 新建文件夹输入对话框 — 450px, 8px radius, input + confirm/cancel
 */
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Disk 1.0

Dialog {
    id: dlg
    title: "📝 新建文件夹"
    modal: true
    width: 450
    anchors.centerIn: parent
    standardButtons: Dialog.NoButton
    padding: 24

    onOpened: {
        nameField.text = ""
        nameField.forceActiveFocus()
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 16

        Label {
            text: "文件夹名称"
            font.pixelSize: 13
        }

        TextField {
            id: nameField
            Layout.fillWidth: true
            placeholderText: "请输入文件夹名称"
            font.pixelSize: 14
            selectByMouse: true

            Keys.onReturnPressed: {
                if (nameField.text.trim().length > 0)
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
                onClicked: dlg.accept()
            }
        }
    }

    onAccepted: {
        var name = nameField.text.trim()
        if (name.length > 0) {
            FileListViewModel.createFolder(name)
        }
    }
}
