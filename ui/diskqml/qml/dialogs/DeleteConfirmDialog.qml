/**
 * @file DeleteConfirmDialog.qml
 * @brief 确认删除对话框 — 400px, 8px radius, ⚠️ icon, confirm/cancel
 */
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Disk 1.0

Dialog {
    id: dlg
    title: "⚠️ 确认删除"
    modal: true
    width: 400
    anchors.centerIn: parent
    standardButtons: Dialog.NoButton
    padding: 24

    /// List of file IDs to delete.
    property var targetFileIds: []
    /// Display name (for single-file deletion).
    property string targetFileName: ""

    function openForFiles(fileIds: var, displayName: string) {
        targetFileIds = fileIds
        targetFileName = displayName
        dlg.open()
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 16

        Label {
            text: {
                if (dlg.targetFileIds.length === 1) {
                    return "确定要删除 \"" + dlg.targetFileName + "\" 吗？"
                }
                return "确定要删除选中的 " + dlg.targetFileIds.length + " 个项目吗？"
            }
            font.pixelSize: 14
            wrapMode: Text.Wrap
            Layout.fillWidth: true
        }

        Label {
            text: "此操作将移入回收站，可在 30 天内恢复。"
            font.pixelSize: 12
            color: palette.placeholderText
            Layout.fillWidth: true
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
                text: "确认删除"
                highlighted: true
                onClicked: dlg.accept()
            }
        }
    }

    onAccepted: {
        FileListViewModel.deleteFiles(dlg.targetFileIds)
    }
}
