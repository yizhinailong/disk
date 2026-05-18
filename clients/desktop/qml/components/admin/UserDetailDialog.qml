import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ".."

Dialog {
    id: root
    modal: true
    width: 480
    title: qsTr("用户详情")
    standardButtons: Dialog.NoButton
    closePolicy: Popup.NoAutoClose

    property int userId: 0
    property string userName: ""
    property string userEmail: ""
    property string userRole: ""
    property string userStatus: ""

    WorkspaceTheme { id: theme }

    ColumnLayout {
        width: parent.width
        spacing: 12

        GridLayout {
            columns: 2
            rowSpacing: 8
            columnSpacing: 16

            Label {
                text: qsTr("用户名:")
                font.bold: true
            }
            Label {
                text: root.userName
                Layout.fillWidth: true
            }

            Label {
                text: qsTr("邮箱:")
                font.bold: true
            }
            Label {
                text: root.userEmail
                Layout.fillWidth: true
            }

            Label {
                text: qsTr("角色:")
                font.bold: true
            }
            Label {
                text: root.userRole
                Layout.fillWidth: true
            }

            Label {
                text: qsTr("状态:")
                font.bold: true
            }
            Label {
                text: root.userStatus
                Layout.fillWidth: true
                color: root.userStatus === qsTr("正常")
                       ? theme.successTextColor
                       : (root.userStatus === qsTr("禁用")
                          ? theme.errorTextColor
                          : theme.warningChipColor)
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
