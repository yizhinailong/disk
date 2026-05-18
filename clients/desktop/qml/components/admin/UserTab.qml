import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ".."
import "../FormatUtils.js" as FormatUtils

pragma ComponentBehavior: Bound

Item {
    id: root

    WorkspaceTheme { id: theme }

    readonly property alias workspaceTheme: theme
    readonly property alias confirmDialogRef: confirmDialog
    readonly property alias userDetailDialogRef: userDetailDialog
    required property var adminManagerRef

    property string searchUsername: ""
    property int filterStatus: -1
    property int filterRole: -1
    property int currentPage: 1
    property int totalPages: 1
    property int totalItems: 0
    property int pageSize: 20
    property var pendingConfirmAction: null

    function statusText(status) {
        switch (status) {
        case 0: return qsTr("禁用")
        case 1: return qsTr("正常")
        case 2: return qsTr("锁定")
        default: return qsTr("未知")
        }
    }

    function statusColor(status) {
        switch (status) {
        case 0: return root.workspaceTheme.errorTextColor
        case 1: return root.workspaceTheme.successTextColor
        case 2: return root.workspaceTheme.warningChipColor
        default: return root.workspaceTheme.mutedTextColor
        }
    }

    function roleText(role) {
        switch (role) {
        case 0: return qsTr("用户")
        case 1: return qsTr("管理员")
        default: return qsTr("未知")
        }
    }

    function applyFilters() {
        root.currentPage = 1
        root.adminManagerRef.ListUsers(root.currentPage, root.pageSize, root.searchUsername, "", root.filterStatus, root.filterRole)
    }

    function goToPage(page) {
        if (page < 1 || page > root.totalPages) return
        root.currentPage = page
        root.adminManagerRef.ListUsers(root.currentPage, root.pageSize, root.searchUsername, "", root.filterStatus, root.filterRole)
    }

    function requestConfirmation(message, action) {
        root.pendingConfirmAction = action
        root.confirmDialogRef.message = message
        root.confirmDialogRef.open()
    }

    Component.onCompleted: {
        root.adminManagerRef.ListUsers(root.currentPage, root.pageSize, "", "", -1, -1)
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: theme.panelSpacing

        // Filter bar
        RowLayout {
            Layout.fillWidth: true
            spacing: theme.compactSpacing

            TextField {
                id: usernameFilterField
                Layout.preferredWidth: 180
                placeholderText: qsTr("搜索用户名")
                onAccepted: {
                    root.searchUsername = text
                    root.applyFilters()
                }
            }

            ComboBox {
                id: statusFilterCombo
                Layout.preferredWidth: 120
                model: [
                    { text: qsTr("全部"), value: -1 },
                    { text: qsTr("禁用"), value: 0 },
                    { text: qsTr("正常"), value: 1 },
                    { text: qsTr("锁定"), value: 2 }
                ]
                textRole: "text"
                valueRole: "value"
                currentIndex: 0
                onActivated: {
                    root.filterStatus = currentValue
                    root.applyFilters()
                }
            }

            ComboBox {
                id: roleFilterCombo
                Layout.preferredWidth: 120
                model: [
                    { text: qsTr("全部"), value: -1 },
                    { text: qsTr("用户"), value: 0 },
                    { text: qsTr("管理员"), value: 1 }
                ]
                textRole: "text"
                valueRole: "value"
                currentIndex: 0
                onActivated: {
                    root.filterRole = currentValue
                    root.applyFilters()
                }
            }

            Item {
                Layout.fillWidth: true
            }

            Button {
                text: qsTr("搜索")
                highlighted: true
                onClicked: {
                    root.searchUsername = usernameFilterField.text
                    root.applyFilters()
                }
            }
        }

        // Table header
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

                Label {
                    Layout.preferredWidth: 60
                    Layout.minimumWidth: 0
                    text: qsTr("ID")
                    font.bold: true
                    font.pixelSize: 12
                    color: theme.tableHeaderTextColor
                    elide: Text.ElideRight
                    wrapMode: Text.NoWrap
                }

                Label {
                    Layout.preferredWidth: 120
                    Layout.minimumWidth: 0
                    text: qsTr("用户名")
                    font.bold: true
                    font.pixelSize: 12
                    color: theme.tableHeaderTextColor
                    elide: Text.ElideRight
                    wrapMode: Text.NoWrap
                }

                Label {
                    Layout.preferredWidth: 180
                    Layout.minimumWidth: 0
                    text: qsTr("邮箱")
                    font.bold: true
                    font.pixelSize: 12
                    color: theme.tableHeaderTextColor
                    elide: Text.ElideRight
                    wrapMode: Text.NoWrap
                }

                Label {
                    Layout.preferredWidth: 80
                    Layout.minimumWidth: 0
                    text: qsTr("角色")
                    font.bold: true
                    font.pixelSize: 12
                    color: theme.tableHeaderTextColor
                    elide: Text.ElideRight
                    wrapMode: Text.NoWrap
                }

                Label {
                    Layout.preferredWidth: 80
                    Layout.minimumWidth: 0
                    text: qsTr("状态")
                    font.bold: true
                    font.pixelSize: 12
                    color: theme.tableHeaderTextColor
                    elide: Text.ElideRight
                    wrapMode: Text.NoWrap
                }

                Label {
                    Layout.preferredWidth: 120
                    Layout.minimumWidth: 0
                    text: qsTr("存储用量")
                    font.bold: true
                    font.pixelSize: 12
                    color: theme.tableHeaderTextColor
                    elide: Text.ElideRight
                    wrapMode: Text.NoWrap
                }

                Label {
                    Layout.preferredWidth: 160
                    Layout.minimumWidth: 0
                    text: qsTr("操作")
                    font.bold: true
                    font.pixelSize: 12
                    color: theme.tableHeaderTextColor
                    elide: Text.ElideRight
                    wrapMode: Text.NoWrap
                }
            }
        }

        // Table body
        ListView {
            id: userListView
            Layout.fillWidth: true
            Layout.fillHeight: true
            model: root.adminManagerRef.userModel
            clip: true
            spacing: 1

            delegate: Rectangle {
                id: userRowDelegate
                required property int index
                required property var userId
                required property string username
                required property string email
                required property int role
                required property int status
                required property var storageUsed

                width: userListView.width
                implicitHeight: rowLayout.implicitHeight + 16
                color: userRowDelegate.index % 2 === 0 ? root.workspaceTheme.panelBackgroundColor : root.workspaceTheme.panelMutedFillColor
                radius: root.workspaceTheme.innerPanelRadius

                MouseArea {
                    anchors.fill: parent
                    onClicked: {
                        root.userDetailDialogRef.userId = userRowDelegate.userId
                        root.userDetailDialogRef.userName = userRowDelegate.username || ""
                        root.userDetailDialogRef.userEmail = userRowDelegate.email || ""
                        root.userDetailDialogRef.userRole = root.roleText(userRowDelegate.role)
                        root.userDetailDialogRef.userStatus = root.statusText(userRowDelegate.status)
                        root.userDetailDialogRef.open()
                    }
                }

                RowLayout {
                    id: rowLayout
                    anchors.fill: parent
                    anchors.leftMargin: 12
                    anchors.rightMargin: 12
                    spacing: root.workspaceTheme.tableColumnSpacing

                    Label {
                        Layout.preferredWidth: 60
                        Layout.minimumWidth: 0
                        text: String(userRowDelegate.userId || "")
                        font.pixelSize: 13
                        color: root.workspaceTheme.tableBodyPrimaryTextColor
                        elide: Text.ElideRight
                        wrapMode: Text.NoWrap
                    }

                    Label {
                        Layout.preferredWidth: 120
                        Layout.minimumWidth: 0
                        text: userRowDelegate.username || ""
                        font.pixelSize: 13
                        color: root.workspaceTheme.tableBodyPrimaryTextColor
                        elide: Text.ElideRight
                        wrapMode: Text.NoWrap
                    }

                    Label {
                        Layout.preferredWidth: 180
                        Layout.minimumWidth: 0
                        text: userRowDelegate.email || ""
                        font.pixelSize: 13
                        color: root.workspaceTheme.tableBodySecondaryTextColor
                        elide: Text.ElideRight
                        wrapMode: Text.NoWrap
                    }

                    Label {
                        Layout.preferredWidth: 80
                        Layout.minimumWidth: 0
                        text: root.roleText(userRowDelegate.role)
                        font.pixelSize: 13
                        color: root.workspaceTheme.tableBodySecondaryTextColor
                        elide: Text.ElideRight
                        wrapMode: Text.NoWrap
                    }

                    Label {
                        Layout.preferredWidth: 80
                        Layout.minimumWidth: 0
                        text: root.statusText(userRowDelegate.status)
                        font.pixelSize: 13
                        color: root.statusColor(userRowDelegate.status)
                        elide: Text.ElideRight
                        wrapMode: Text.NoWrap
                    }

                    Label {
                        Layout.preferredWidth: 120
                        Layout.minimumWidth: 0
                        text: FormatUtils.formatStorageSize(userRowDelegate.storageUsed || 0)
                        font.pixelSize: 13
                        color: root.workspaceTheme.tableBodySecondaryTextColor
                        elide: Text.ElideRight
                        wrapMode: Text.NoWrap
                    }

                    RowLayout {
                        Layout.preferredWidth: 160
                        Layout.minimumWidth: 0
                        spacing: 4

                        Button {
                            text: qsTr("修改状态")
                            flat: true
                            font.pixelSize: 12
                            onClicked: statusMenu.popup()

                            Menu {
                                id: statusMenu

                                MenuItem {
                                    text: qsTr("启用")
                                    onTriggered: {
                                        var userId = userRowDelegate.userId
                                        var username = userRowDelegate.username || ""
                                        root.requestConfirmation(qsTr("确定启用用户 %1 吗？").arg(username), function() {
                                            root.adminManagerRef.ChangeUserStatus(userId, 1)
                                        })
                                    }
                                }
                                MenuItem {
                                    text: qsTr("禁用")
                                    onTriggered: {
                                        var userId = userRowDelegate.userId
                                        var username = userRowDelegate.username || ""
                                        root.requestConfirmation(qsTr("确定禁用用户 %1 吗？").arg(username), function() {
                                            root.adminManagerRef.ChangeUserStatus(userId, 0)
                                        })
                                    }
                                }
                                MenuItem {
                                    text: qsTr("锁定")
                                    onTriggered: {
                                        var userId = userRowDelegate.userId
                                        var username = userRowDelegate.username || ""
                                        root.requestConfirmation(qsTr("确定锁定用户 %1 吗？").arg(username), function() {
                                            root.adminManagerRef.ChangeUserStatus(userId, 2)
                                        })
                                    }
                                }
                            }
                        }

                        Button {
                            text: qsTr("修改角色")
                            flat: true
                            font.pixelSize: 12
                            onClicked: {
                                var userId = userRowDelegate.userId
                                var username = userRowDelegate.username || ""
                                var newRole = userRowDelegate.role === 0 ? 1 : 0
                                var newRoleText = newRole === 0 ? qsTr("用户") : qsTr("管理员")
                                root.requestConfirmation(qsTr("确定将用户 %1 的角色修改为 %2 吗？").arg(username).arg(newRoleText), function() {
                                    root.adminManagerRef.ChangeUserRole(userId, newRole)
                                })
                            }
                        }

                        Button {
                            text: qsTr("删除")
                            flat: true
                            font.pixelSize: 12
                            palette.buttonText: root.workspaceTheme.errorTextColor
                            onClicked: {
                                var userId = userRowDelegate.userId
                                var username = userRowDelegate.username || ""
                                root.requestConfirmation(qsTr("确定删除用户 %1 吗？此操作不可撤销。").arg(username), function() {
                                    root.adminManagerRef.SoftDeleteUser(userId)
                                })
                            }
                        }
                    }
                }
            }

            Label {
                anchors.centerIn: parent
                text: qsTr("暂无用户数据")
                color: theme.mutedTextColor
                visible: userListView.count === 0
            }
        }

        // Pagination
        RowLayout {
            Layout.fillWidth: true
            spacing: theme.compactSpacing

            Item {
                Layout.fillWidth: true
            }

            Button {
                text: qsTr("上一页")
                enabled: root.currentPage > 1
                onClicked: root.goToPage(root.currentPage - 1)
            }

            Label {
                text: qsTr("第 %1 / %2 页 (共 %3 条)").arg(root.currentPage).arg(root.totalPages).arg(root.totalItems)
                color: theme.secondaryTextColor
            }

            Button {
                text: qsTr("下一页")
                enabled: root.currentPage < root.totalPages
                onClicked: root.goToPage(root.currentPage + 1)
            }
        }
    }

    ConfirmDialog {
        id: confirmDialog
        title: qsTr("确认操作")

        onConfirmed: {
            if (root.pendingConfirmAction) {
                root.pendingConfirmAction()
                root.pendingConfirmAction = null
            }
        }

        onCancelled: root.pendingConfirmAction = null
        onClosed: root.pendingConfirmAction = null
    }

    UserDetailDialog {
        id: userDetailDialog
    }

    Connections {
        target: root.adminManagerRef
        ignoreUnknownSignals: true

        function onUserPaginationLoaded(page, totalPages, total) {
            root.currentPage = page
            root.totalPages = totalPages
            root.totalItems = total
        }

        function onOperationSuccess(_message) {
            root.goToPage(root.currentPage)
        }
    }
}
