import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ".."

Dialog {
    id: root
    objectName: "fileDetailDialog"
    modal: true
    width: 520
    title: "文件详情"
    standardButtons: Dialog.NoButton
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    property var detail: ({})
    property var formatSize: function(bytes) { return String(bytes || 0) + " B" }
    property var formatType: function(kind, mimeType) { return mimeType || kind || "未知" }
    property var formatDateTime: function(value) { return value ? String(value) : "—" }

    WorkspaceTheme { id: theme }

    function valueOrDash(value) {
        if (value === undefined || value === null || value === "") {
            return "—"
        }
        return String(value)
    }

    ColumnLayout {
        width: parent.width
        spacing: 12

        GridLayout {
            Layout.fillWidth: true
            columns: 2
            rowSpacing: 8
            columnSpacing: 16

            Label { text: "名称："; font.bold: true }
            Label {
                Layout.fillWidth: true
                text: root.valueOrDash(root.detail.name)
                wrapMode: Text.WrapAnywhere
            }

            Label { text: "类型："; font.bold: true }
            Label {
                Layout.fillWidth: true
                text: root.formatType(root.detail.type, root.detail.mime_type)
                wrapMode: Text.WrapAnywhere
            }

            Label { text: "大小："; font.bold: true }
            Label {
                Layout.fillWidth: true
                text: root.formatSize(root.detail.size || 0)
            }

            Label { text: "路径："; font.bold: true }
            Label {
                Layout.fillWidth: true
                text: root.valueOrDash(root.detail.path)
                wrapMode: Text.WrapAnywhere
            }

            Label { text: "哈希："; font.bold: true }
            Label {
                Layout.fillWidth: true
                text: root.valueOrDash(root.detail.hash)
                wrapMode: Text.WrapAnywhere
            }

            Label { text: "MIME："; font.bold: true }
            Label {
                Layout.fillWidth: true
                text: root.valueOrDash(root.detail.mime_type)
                wrapMode: Text.WrapAnywhere
            }

            Label { text: "父文件夹："; font.bold: true }
            Label {
                Layout.fillWidth: true
                text: root.detail.parent_id !== undefined ? String(root.detail.parent_id) : "—"
            }

            Label { text: "创建时间："; font.bold: true }
            Label {
                Layout.fillWidth: true
                text: root.formatDateTime(root.detail.created_at)
            }

            Label { text: "更新时间："; font.bold: true }
            Label {
                Layout.fillWidth: true
                text: root.formatDateTime(root.detail.updated_at)
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Item { Layout.fillWidth: true }
            Button {
                text: "关闭"
                onClicked: root.close()
            }
        }
    }
}
