import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ".."

Dialog {
    id: root
    objectName: "ownerShareDetailDialog"
    modal: true
    width: 560
    height: 520
    title: "分享详情"
    standardButtons: Dialog.NoButton
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    property var detail: ({})
    property var formatSize: function(bytes) { return String(bytes || 0) + " B" }
    property var formatPermission: function(permission) { return String(permission || "—") }
    property var formatStatus: function(status) { return String(status || "—") }
    property var formatDateTime: function(value, fallback) { return value ? String(value) : fallback }

    WorkspaceTheme { id: theme }

    function valueOrDash(value) {
        if (value === undefined || value === null || value === "") {
            return "—"
        }
        return String(value)
    }

    ColumnLayout {
        width: parent.width
        height: parent.height
        spacing: 12

        GridLayout {
            Layout.fillWidth: true
            columns: 2
            rowSpacing: 8
            columnSpacing: 16

            Label { text: "分享码："; font.bold: true }
            Label {
                Layout.fillWidth: true
                text: root.valueOrDash(root.detail.share_id)
                wrapMode: Text.WrapAnywhere
            }

            Label { text: "分享链接："; font.bold: true }
            Label {
                Layout.fillWidth: true
                text: root.valueOrDash(root.detail.share_link)
                wrapMode: Text.WrapAnywhere
            }

            Label { text: "权限："; font.bold: true }
            Label { Layout.fillWidth: true; text: root.formatPermission(root.detail.permission) }

            Label { text: "状态："; font.bold: true }
            Label { Layout.fillWidth: true; text: root.formatStatus(root.detail.status) }

            Label { text: "密码保护："; font.bold: true }
            Label { Layout.fillWidth: true; text: root.detail.has_password ? "是" : "否" }

            Label { text: "浏览次数："; font.bold: true }
            Label { Layout.fillWidth: true; text: String(root.detail.view_count || 0) }

            Label { text: "下载次数："; font.bold: true }
            Label { Layout.fillWidth: true; text: String(root.detail.download_count || 0) }

            Label { text: "创建时间："; font.bold: true }
            Label { Layout.fillWidth: true; text: root.formatDateTime(root.detail.created_at, "—") }

            Label { text: "过期时间："; font.bold: true }
            Label { Layout.fillWidth: true; text: root.formatDateTime(root.detail.expires_at, "永久有效") }
        }

        Label {
            Layout.fillWidth: true
            text: "分享文件"
            font.bold: true
            color: theme.strongTextColor
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: theme.panelMutedFillColor
            radius: theme.innerPanelRadius
            border.color: theme.panelBorderColor

            ListView {
                id: shareFileListView
                objectName: "ownerShareDetailFileList"
                anchors.fill: parent
                anchors.margins: 8
                clip: true
                model: root.detail.files || []

                delegate: ItemDelegate {
                    width: ListView.view.width
                    text: modelData.name + "  ·  " + root.formatSize(modelData.size || 0)
                }

                Label {
                    anchors.centerIn: parent
                    text: "暂无文件"
                    color: theme.mutedTextColor
                    visible: shareFileListView.count === 0
                }
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
