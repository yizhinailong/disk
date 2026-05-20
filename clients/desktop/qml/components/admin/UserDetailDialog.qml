import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ".."
import "../FormatUtils.js" as FormatUtils

Dialog {
    id: root
    modal: true
    width: 520
    title: qsTr("用户详情")
    standardButtons: Dialog.NoButton
    closePolicy: Popup.NoAutoClose

    property int userId: 0
    property string userName: ""
    property string userEmail: ""
    property string userNickname: ""
    property string userRole: ""
    property string userStatus: ""
    property var storageQuota: 0
    property var storageUsed: 0
    property string createdAt: ""
    property string lastLoginAt: ""

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
            columns: 2
            rowSpacing: 8
            columnSpacing: 16

            Label { text: qsTr("ID:"); font.bold: true }
            Label { text: root.userId > 0 ? String(root.userId) : "—"; Layout.fillWidth: true }

            Label { text: qsTr("用户名:"); font.bold: true }
            Label { text: root.valueOrDash(root.userName); Layout.fillWidth: true }

            Label { text: qsTr("邮箱:"); font.bold: true }
            Label { text: root.valueOrDash(root.userEmail); Layout.fillWidth: true }

            Label { text: qsTr("昵称:"); font.bold: true }
            Label { text: root.valueOrDash(root.userNickname); Layout.fillWidth: true }

            Label { text: qsTr("角色:"); font.bold: true }
            Label { text: root.valueOrDash(root.userRole); Layout.fillWidth: true }

            Label { text: qsTr("状态:"); font.bold: true }
            Label {
                text: root.valueOrDash(root.userStatus)
                Layout.fillWidth: true
                color: root.userStatus === qsTr("正常")
                       ? theme.successTextColor
                       : (root.userStatus === qsTr("禁用")
                          ? theme.errorTextColor
                          : theme.warningChipColor)
            }

            Label { text: qsTr("存储用量:"); font.bold: true }
            Label { text: FormatUtils.formatStorageSize(root.storageUsed || 0); Layout.fillWidth: true }

            Label { text: qsTr("存储配额:"); font.bold: true }
            Label { text: FormatUtils.formatStorageSize(root.storageQuota || 0); Layout.fillWidth: true }

            Label { text: qsTr("创建时间:"); font.bold: true }
            Label { text: root.valueOrDash(root.createdAt); Layout.fillWidth: true }

            Label { text: qsTr("最后登录:"); font.bold: true }
            Label { text: root.valueOrDash(root.lastLoginAt); Layout.fillWidth: true }
        }

        RowLayout {
            Layout.fillWidth: true
            Item { Layout.fillWidth: true }
            Button {
                text: qsTr("关闭")
                onClicked: root.close()
            }
        }
    }
}
