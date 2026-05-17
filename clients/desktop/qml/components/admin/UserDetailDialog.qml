import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ".."
import "../FormatUtils.js" as FormatUtils

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
    property var userStorage: ({})
    property var userFiles: []

    WorkspaceTheme { id: theme }

    onOpened: {
        if (userId > 0) {
            adminManager.ListUserFiles(userId)
            adminManager.GetUserStorage(userId)
        }
    }

    Connections {
        target: adminManager
        ignoreUnknownSignals: true

        function onUserStorageLoaded(storage) {
            if (root.visible && storage) {
                root.userStorage = storage
            }
        }
    }

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

        Rectangle {
            Layout.fillWidth: true
            height: 1
            color: theme.panelBorderColor
        }

        Label {
            text: qsTr("存储统计")
            font.bold: true
            font.pixelSize: 14
        }

        Label {
            text: {
                var used = root.userStorage.used || 0
                var quota = root.userStorage.quota || 0
                return FormatUtils.formatStorageSize(used) + " / " + FormatUtils.formatStorageSize(quota)
            }
            color: theme.secondaryTextColor
        }

        Rectangle {
            Layout.fillWidth: true
            height: 1
            color: theme.panelBorderColor
        }

        Label {
            text: qsTr("用户文件")
            font.bold: true
            font.pixelSize: 14
        }

        ListView {
            Layout.fillWidth: true
            Layout.preferredHeight: 120
            model: root.userFiles
            clip: true
            delegate: Label {
                text: modelData.name || "—"
                color: theme.secondaryTextColor
            }
            visible: root.userFiles.length > 0
        }

        Label {
            text: qsTr("暂无文件")
            visible: root.userFiles.length === 0
            color: theme.mutedTextColor
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
