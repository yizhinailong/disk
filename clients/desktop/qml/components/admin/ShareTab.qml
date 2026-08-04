import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ".."

Item {
    id: root

    WorkspaceTheme { id: theme }

    property bool isActive: true
    property int filterStatus: -1
    property int currentPage: 1
    property int totalPages: 1
    property int totalItems: 0
    property int pageSize: 20
    property string searchUsername: ""
    property bool isLoadingShares: false
    property bool hasLoadedShares: false
    property string shareLoadError: ""
    property var pendingConfirmAction: null

    function statusText(status) {
        switch (status) {
        case 0: return qsTr("已取消")
        case 1: return qsTr("有效")
        case 2: return qsTr("已过期")
        default: return qsTr("未知")
        }
    }

    function statusColor(status) {
        switch (status) {
        case 0: return theme.mutedTextColor
        case 1: return theme.successTextColor
        case 2: return theme.errorTextColor
        default: return theme.mutedTextColor
        }
    }

    function formatDateTime(value) {
        if (value === undefined || value === null || value === "") {
            return "—"
        }
        var formatted = Qt.formatDateTime(value, "yyyy-MM-dd hh:mm")
        return formatted !== "" ? formatted : String(value)
    }

    function requestShares() {
        root.isLoadingShares = true
        root.shareLoadError = ""
        adminManager.ListShares(root.currentPage, root.pageSize, root.filterStatus, -1, root.searchUsername)
    }

    function applyFilters() {
        root.currentPage = 1
        root.requestShares()
    }

    function applySharerSearch() {
        root.searchUsername = sharerSearchField.text.trim()
        root.applyFilters()
    }

    function goToPage(page) {
        if (page < 1 || page > root.totalPages) return
        root.currentPage = page
        root.requestShares()
    }

    function requestConfirmation(message, action) {
        root.pendingConfirmAction = action
        confirmDialog.message = message
        confirmDialog.open()
    }

    Component.onCompleted: {
        root.requestShares()
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: theme.panelSpacing

        // Filter bar
        RowLayout {
            Layout.fillWidth: true
            spacing: theme.compactSpacing

            TextField {
                id: sharerSearchField
                Layout.preferredWidth: 180
                placeholderText: qsTr("搜索分享者")
                text: root.searchUsername
                selectByMouse: true
                onAccepted: root.applySharerSearch()
            }

            Button {
                text: qsTr("搜索")
                highlighted: true
                onClicked: root.applySharerSearch()
            }

            ComboBox {
                id: statusFilterCombo
                Layout.preferredWidth: 140
                model: [
                    { text: qsTr("全部"), value: -1 },
                    { text: qsTr("有效"), value: 1 },
                    { text: qsTr("已过期"), value: 2 },
                    { text: qsTr("已取消"), value: 0 }
                ]
                textRole: "text"
                valueRole: "value"
                currentIndex: 0
                onActivated: {
                    root.filterStatus = currentValue
                    root.applyFilters()
                }
            }

            Item {
                Layout.fillWidth: true
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
                    Layout.preferredWidth: 100
                    Layout.minimumWidth: 0
                    text: qsTr("分享者")
                    font.bold: true
                    font.pixelSize: 12
                    color: theme.tableHeaderTextColor
                    elide: Text.ElideRight
                    wrapMode: Text.NoWrap
                }

                Label {
                    Layout.preferredWidth: 140
                    Layout.minimumWidth: 0
                    text: qsTr("文件名")
                    font.bold: true
                    font.pixelSize: 12
                    color: theme.tableHeaderTextColor
                    elide: Text.ElideRight
                    wrapMode: Text.NoWrap
                }

                Label {
                    Layout.preferredWidth: 100
                    Layout.minimumWidth: 0
                    text: qsTr("分享码")
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
                    Layout.preferredWidth: 80
                    Layout.minimumWidth: 0
                    text: qsTr("访问次数")
                    font.bold: true
                    font.pixelSize: 12
                    color: theme.tableHeaderTextColor
                    elide: Text.ElideRight
                    wrapMode: Text.NoWrap
                }

                Label {
                    Layout.preferredWidth: 140
                    Layout.minimumWidth: 0
                    text: qsTr("创建时间")
                    font.bold: true
                    font.pixelSize: 12
                    color: theme.tableHeaderTextColor
                    elide: Text.ElideRight
                    wrapMode: Text.NoWrap
                }

                Label {
                    Layout.preferredWidth: 120
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
            id: shareListView
            Layout.fillWidth: true
            Layout.fillHeight: true
            model: adminManager.shareModel
            clip: true
            spacing: 1

            delegate: Rectangle {
                id: shareRowDelegate
                width: shareListView.width
                implicitHeight: rowLayout.implicitHeight + 16
                color: index % 2 === 0 ? theme.panelBackgroundColor : theme.panelMutedFillColor
                radius: theme.innerPanelRadius

                RowLayout {
                    id: rowLayout
                    anchors.fill: parent
                    anchors.leftMargin: 12
                    anchors.rightMargin: 12
                    spacing: theme.tableColumnSpacing

                    Label {
                        Layout.preferredWidth: 100
                        Layout.minimumWidth: 0
                        text: model.username || ""
                        font.pixelSize: 13
                        color: theme.tableBodyPrimaryTextColor
                        elide: Text.ElideRight
                        wrapMode: Text.NoWrap
                    }

                    Label {
                        Layout.preferredWidth: 140
                        Layout.minimumWidth: 0
                        text: model.fileName || ""
                        font.pixelSize: 13
                        color: theme.tableBodySecondaryTextColor
                        elide: Text.ElideRight
                        wrapMode: Text.NoWrap
                    }

                    Label {
                        Layout.preferredWidth: 100
                        Layout.minimumWidth: 0
                        text: model.shareId || ""
                        font.pixelSize: 13
                        color: theme.tableBodySecondaryTextColor
                        elide: Text.ElideRight
                        wrapMode: Text.NoWrap
                    }

                    Label {
                        Layout.preferredWidth: 80
                        Layout.minimumWidth: 0
                        text: root.statusText(model.status)
                        font.pixelSize: 13
                        color: root.statusColor(model.status)
                        elide: Text.ElideRight
                        wrapMode: Text.NoWrap
                    }

                    Label {
                        Layout.preferredWidth: 80
                        Layout.minimumWidth: 0
                        text: String(model.accessCount || 0)
                        font.pixelSize: 13
                        color: theme.tableBodySecondaryTextColor
                        elide: Text.ElideRight
                        wrapMode: Text.NoWrap
                    }

                    Label {
                        Layout.preferredWidth: 140
                        Layout.minimumWidth: 0
                        text: root.formatDateTime(model.createdAt)
                        font.pixelSize: 13
                        color: theme.tableBodyTertiaryTextColor
                        elide: Text.ElideRight
                        wrapMode: Text.NoWrap
                    }

                    RowLayout {
                        Layout.preferredWidth: 120
                        Layout.minimumWidth: 0
                        spacing: 4

                        Button {
                            text: qsTr("查看详情")
                            flat: true
                            font.pixelSize: 12
                            onClicked: adminManager.GetShareDetail(model.shareId)
                        }

                        Button {
                            text: qsTr("强制取消")
                            flat: true
                            font.pixelSize: 12
                            palette.buttonText: theme.errorTextColor
                            enabled: model.status !== 0
                            onClicked: {
                                var shareId = model.shareId || ""
                                root.requestConfirmation(qsTr("确定强制取消分享 %1 吗？").arg(shareId), function() {
                                    adminManager.ForceCancelShare(shareId)
                                })
                            }
                        }
                    }
                }
            }

            BusyIndicator {
                anchors.centerIn: parent
                running: visible
                visible: root.isLoadingShares && shareListView.count === 0
            }

            Label {
                anchors.centerIn: parent
                text: root.shareLoadError !== "" ? root.shareLoadError : qsTr("暂无分享数据")
                color: root.shareLoadError !== "" ? theme.errorTextColor : theme.mutedTextColor
                visible: shareListView.count === 0
                         && ((root.hasLoadedShares && !root.isLoadingShares && root.shareLoadError === "")
                             || root.shareLoadError !== "")
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

    ShareDetailDialog {
        id: shareDetailDialog
    }

    Connections {
        target: adminManager
        ignoreUnknownSignals: true

        function onSharePaginationLoaded(page, totalPages, total) {
            root.currentPage = page
            root.totalPages = totalPages
            root.totalItems = total
            root.isLoadingShares = false
            root.hasLoadedShares = true
            root.shareLoadError = ""
        }

        function onShareDetailLoaded(detail) {
            shareDetailDialog.shareId = detail.share_id || ""
            shareDetailDialog.userName = detail.username || ""
            shareDetailDialog.fileName = detail.file_name || ""
            shareDetailDialog.status = detail.status || 0
            shareDetailDialog.accessCount = detail.access_count || 0
            shareDetailDialog.createdAt = detail.created_at || ""
            shareDetailDialog.expiresAt = detail.expires_at || ""
            shareDetailDialog.passwordSet = detail.password_set || false
            shareDetailDialog.open()
        }

        function onOperationSuccess(_message) {
            if (root.isActive) {
                root.goToPage(root.currentPage)
            }
        }

        function onApiError(message, code) {
            if (!root.isLoadingShares) {
                return
            }
            root.isLoadingShares = false
            root.hasLoadedShares = true
            root.shareLoadError = message || qsTr("加载分享数据失败")
        }
    }
}
