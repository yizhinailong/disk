import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ".."

Dialog {
    id: root
    modal: true
    width: 480
    title: qsTr("分享详情")
    standardButtons: Dialog.NoButton
    closePolicy: Popup.NoAutoClose

    property int shareId: 0
    property string shareCode: ""
    property string userName: ""
    property string fileName: ""
    property int status: 0
    property int accessCount: 0
    property string createdAt: ""
    property string expiresAt: ""
    property bool passwordSet: false

    WorkspaceTheme { id: theme }

    function statusText(s) {
        if (s === 1) return qsTr("活跃")
        if (s === 2) return qsTr("已过期")
        return qsTr("已取消")
    }

    function statusColor(s) {
        if (s === 1) return theme.successTextColor
        if (s === 2) return theme.errorTextColor
        return theme.warningChipColor
    }

    ColumnLayout {
        width: parent.width
        spacing: 12

        GridLayout {
            columns: 2
            rowSpacing: 8
            columnSpacing: 16

            Label {
                text: qsTr("分享码:")
                font.bold: true
            }
            TextEdit {
                text: root.shareCode
                Layout.fillWidth: true
                readOnly: true
                selectByMouse: true
                wrapMode: TextEdit.Wrap
            }

            Label {
                text: qsTr("分享用户:")
                font.bold: true
            }
            Label {
                text: root.userName
                Layout.fillWidth: true
            }

            Label {
                text: qsTr("文件名称:")
                font.bold: true
            }
            Label {
                text: root.fileName
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
            }

            Label {
                text: qsTr("状态:")
                font.bold: true
            }
            Label {
                text: root.statusText(root.status)
                color: root.statusColor(root.status)
                Layout.fillWidth: true
            }

            Label {
                text: qsTr("访问次数:")
                font.bold: true
            }
            Label {
                text: root.accessCount.toString()
                Layout.fillWidth: true
            }

            Label {
                text: qsTr("创建时间:")
                font.bold: true
            }
            Label {
                text: root.createdAt
                Layout.fillWidth: true
            }

            Label {
                text: qsTr("过期时间:")
                font.bold: true
            }
            Label {
                text: root.expiresAt || qsTr("永不过期")
                Layout.fillWidth: true
            }

            Label {
                text: qsTr("访问密码:")
                font.bold: true
            }
            Label {
                text: root.passwordSet ? qsTr("已设置") : qsTr("未设置")
                Layout.fillWidth: true
            }
        }

        RowLayout {
            Layout.fillWidth: true

            Item {
                Layout.fillWidth: true
            }

            Button {
                text: qsTr("关闭")
                onClicked: root.close()
            }
        }
    }
}
