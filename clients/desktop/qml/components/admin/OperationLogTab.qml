import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ".."

Item {
    id: root

    required property var adminManagerRef

    WorkspaceTheme { id: theme }

    property int currentPage: 1
    property int totalPages: 1
    property int totalItems: 0
    property int pageSize: 20

    function actionText(action) {
        switch (action) {
        case "login": return qsTr("登录")
        case "logout": return qsTr("退出")
        case "upload": return qsTr("上传")
        case "download": return qsTr("下载")
        case "delete": return qsTr("删除")
        case "rename": return qsTr("重命名")
        case "move": return qsTr("移动")
        case "copy": return qsTr("复制")
        case "share": return qsTr("分享")
        case "restore": return qsTr("恢复")
        default: return action || qsTr("未知")
        }
    }

    function targetTypeText(targetType) {
        switch (targetType) {
        case "file": return qsTr("文件")
        case "folder": return qsTr("文件夹")
        case "share": return qsTr("分享")
        case "user": return qsTr("用户")
        case "unknown": return qsTr("—")
        default: return targetType || qsTr("—")
        }
    }

    function openLogDetail(logId, action, targetType, targetId, targetName, details, ipAddress, createdAt) {
        operationLogDetailDialog.logId = Number(logId || 0)
        operationLogDetailDialog.actionName = root.actionText(action)
        operationLogDetailDialog.rawAction = action || ""
        operationLogDetailDialog.targetTypeName = root.targetTypeText(targetType)
        operationLogDetailDialog.rawTargetType = targetType || ""
        operationLogDetailDialog.targetId = Number(targetId || 0)
        operationLogDetailDialog.targetName = targetName || ""
        operationLogDetailDialog.details = details || ""
        operationLogDetailDialog.ipAddress = ipAddress || ""
        operationLogDetailDialog.createdAt = createdAt || ""
        operationLogDetailDialog.open()
    }

    function goToPage(page) {
        if (page < 1 || page > root.totalPages) {
            return
        }
        root.currentPage = page
        root.adminManagerRef.ListOperationLogs(root.currentPage, root.pageSize)
    }

    Component.onCompleted: root.adminManagerRef.ListOperationLogs(root.currentPage, root.pageSize)

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: theme.pagePadding
        spacing: theme.panelSpacing

        RowLayout {
            Layout.fillWidth: true
            spacing: theme.compactSpacing

            Label {
                text: qsTr("操作日志")
                color: theme.strongTextColor
                font.pixelSize: 16
                font.bold: true
            }

            Label {
                text: qsTr("共 %1 条").arg(root.totalItems)
                color: theme.mutedTextColor
                font.pixelSize: 12
            }

            Item { Layout.fillWidth: true }

            Button {
                text: qsTr("刷新")
                highlighted: true
                onClicked: root.adminManagerRef.ListOperationLogs(root.currentPage, root.pageSize)
            }
        }

        Rectangle {
            Layout.fillWidth: true
            color: theme.panelMutedFillColor
            radius: theme.innerPanelRadius
            implicitHeight: headerRow.implicitHeight + 16

            RowLayout {
                id: headerRow
                anchors.fill: parent
                anchors.leftMargin: 12
                anchors.rightMargin: 12
                spacing: theme.tableColumnSpacing

                Label { Layout.preferredWidth: 80; text: qsTr("ID"); font.bold: true; color: theme.tableHeaderTextColor }
                Label { Layout.preferredWidth: 100; text: qsTr("操作"); font.bold: true; color: theme.tableHeaderTextColor }
                Label { Layout.preferredWidth: 100; text: qsTr("对象类型"); font.bold: true; color: theme.tableHeaderTextColor }
                Label { Layout.fillWidth: true; text: qsTr("对象"); font.bold: true; color: theme.tableHeaderTextColor }
                Label { Layout.preferredWidth: 160; text: qsTr("IP 地址"); font.bold: true; color: theme.tableHeaderTextColor }
                Label { Layout.preferredWidth: 180; text: qsTr("时间"); font.bold: true; color: theme.tableHeaderTextColor }
                Label { Layout.preferredWidth: 90; text: qsTr("操作"); font.bold: true; color: theme.tableHeaderTextColor }
            }
        }

        ListView {
            id: operationLogListView
            objectName: "operationLogListView"
            Layout.fillWidth: true
            Layout.fillHeight: true
            model: root.adminManagerRef.operationLogModel
            clip: true
            spacing: 1

            delegate: Rectangle {
                id: logRowDelegate
                required property int index
                required property var id
                required property string action
                required property string targetType
                required property var targetId
                required property string targetName
                required property string details
                required property string ipAddress
                required property string createdAt

                width: operationLogListView.width
                implicitHeight: rowLayout.implicitHeight + 16
                color: logRowDelegate.index % 2 === 0 ? theme.panelBackgroundColor : theme.panelMutedFillColor
                radius: theme.innerPanelRadius

                MouseArea {
                    anchors.fill: parent
                    onDoubleClicked: root.openLogDetail(
                        logRowDelegate.id,
                        logRowDelegate.action,
                        logRowDelegate.targetType,
                        logRowDelegate.targetId,
                        logRowDelegate.targetName,
                        logRowDelegate.details,
                        logRowDelegate.ipAddress,
                        logRowDelegate.createdAt
                    )
                }

                RowLayout {
                    id: rowLayout
                    anchors.fill: parent
                    anchors.leftMargin: 12
                    anchors.rightMargin: 12
                    spacing: theme.tableColumnSpacing

                    Label {
                        Layout.preferredWidth: 80
                        text: String(logRowDelegate.id || "")
                        color: theme.tableBodyPrimaryTextColor
                        elide: Text.ElideRight
                    }

                    Label {
                        Layout.preferredWidth: 100
                        text: root.actionText(logRowDelegate.action)
                        color: theme.tableBodyPrimaryTextColor
                        elide: Text.ElideRight
                    }

                    Label {
                        Layout.preferredWidth: 100
                        text: root.targetTypeText(logRowDelegate.targetType)
                        color: theme.tableBodySecondaryTextColor
                        elide: Text.ElideRight
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        spacing: 2

                        Label {
                            Layout.fillWidth: true
                            text: logRowDelegate.targetName || (logRowDelegate.targetId ? String(logRowDelegate.targetId) : "—")
                            color: theme.tableBodyPrimaryTextColor
                            elide: Text.ElideRight
                        }

                        Label {
                            Layout.fillWidth: true
                            text: logRowDelegate.details || ""
                            color: theme.tableBodyTertiaryTextColor
                            font.pixelSize: 11
                            elide: Text.ElideRight
                            visible: text !== ""
                        }
                    }

                    Label {
                        Layout.preferredWidth: 160
                        text: logRowDelegate.ipAddress || "—"
                        color: theme.tableBodySecondaryTextColor
                        elide: Text.ElideRight
                    }

                    Label {
                        Layout.preferredWidth: 180
                        text: logRowDelegate.createdAt || "—"
                        color: theme.tableBodyTertiaryTextColor
                        elide: Text.ElideRight
                    }

                    Button {
                        Layout.preferredWidth: 90
                        text: qsTr("查看详情")
                        onClicked: root.openLogDetail(
                            logRowDelegate.id,
                            logRowDelegate.action,
                            logRowDelegate.targetType,
                            logRowDelegate.targetId,
                            logRowDelegate.targetName,
                            logRowDelegate.details,
                            logRowDelegate.ipAddress,
                            logRowDelegate.createdAt
                        )
                    }
                }
            }

            Label {
                anchors.centerIn: parent
                text: qsTr("暂无操作日志")
                color: theme.mutedTextColor
                visible: operationLogListView.count === 0
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: theme.compactSpacing

            Item { Layout.fillWidth: true }

            Button {
                text: qsTr("上一页")
                enabled: root.currentPage > 1
                onClicked: root.goToPage(root.currentPage - 1)
            }

            Label {
                text: qsTr("第 %1 / %2 页").arg(root.currentPage).arg(root.totalPages)
                color: theme.secondaryTextColor
            }

            Button {
                text: qsTr("下一页")
                enabled: root.currentPage < root.totalPages
                onClicked: root.goToPage(root.currentPage + 1)
            }
        }
    }

    OperationLogDetailDialog {
        id: operationLogDetailDialog
        anchors.centerIn: Overlay.overlay
    }

    Connections {
        target: root.adminManagerRef
        ignoreUnknownSignals: true

        function onOperationLogPaginationLoaded(page, totalPages, total) {
            root.currentPage = page
            root.totalPages = totalPages
            root.totalItems = total
        }
    }
}
