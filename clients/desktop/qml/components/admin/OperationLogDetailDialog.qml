import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ".."

Dialog {
    id: root
    modal: true
    width: 560
    title: qsTr("操作日志详情")
    standardButtons: Dialog.NoButton
    closePolicy: Popup.NoAutoClose

    property int logId: 0
    property string actionName: ""
    property string rawAction: ""
    property string targetTypeName: ""
    property string rawTargetType: ""
    property int targetId: 0
    property string targetName: ""
    property string details: ""
    property string ipAddress: ""
    property string createdAt: ""

    WorkspaceTheme { id: theme }

    function emptyText(value) {
        return value ? value : qsTr("—")
    }

    ColumnLayout {
        width: parent.width
        spacing: 12

        GridLayout {
            Layout.fillWidth: true
            columns: 2
            rowSpacing: 8
            columnSpacing: 16

            Label { text: qsTr("日志ID:"); font.bold: true }
            Label {
                text: String(root.logId || "")
                Layout.fillWidth: true
            }

            Label { text: qsTr("操作:"); font.bold: true }
            Label {
                text: root.emptyText(root.actionName)
                Layout.fillWidth: true
            }

            Label { text: qsTr("原始操作:"); font.bold: true }
            Label {
                text: root.emptyText(root.rawAction)
                Layout.fillWidth: true
            }

            Label { text: qsTr("对象类型:"); font.bold: true }
            Label {
                text: root.emptyText(root.targetTypeName)
                Layout.fillWidth: true
            }

            Label { text: qsTr("原始对象类型:"); font.bold: true }
            Label {
                text: root.emptyText(root.rawTargetType)
                Layout.fillWidth: true
            }

            Label { text: qsTr("对象ID:"); font.bold: true }
            Label {
                text: root.targetId > 0 ? String(root.targetId) : qsTr("—")
                Layout.fillWidth: true
            }

            Label { text: qsTr("对象名称:"); font.bold: true }
            Label {
                text: root.emptyText(root.targetName)
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
            }

            Label { text: qsTr("IP 地址:"); font.bold: true }
            Label {
                text: root.emptyText(root.ipAddress)
                Layout.fillWidth: true
            }

            Label { text: qsTr("创建时间:"); font.bold: true }
            Label {
                text: root.emptyText(root.createdAt)
                Layout.fillWidth: true
            }

            Label {
                text: qsTr("详情:")
                font.bold: true
                Layout.alignment: Qt.AlignTop
            }
            TextEdit {
                text: root.emptyText(root.details)
                Layout.fillWidth: true
                readOnly: true
                selectByMouse: true
                wrapMode: TextEdit.Wrap
                color: theme.strongTextColor
            }
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
