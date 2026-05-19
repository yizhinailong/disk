import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ".."

PageStateView {
    id: root

    required property var page
    property var breadcrumbPath: []

    visible: page.isMyFilesMode
    pageState: shellController.pageState

    emptyText: "此文件夹为空"
    errorText: "加载文件夹内容失败"

    onRetryClicked: page.refreshCurrentFolder()

    Rectangle {
        anchors.fill: parent
        color: page.panelBackgroundColor
        radius: page.panelRadius
        border.color: page.panelBorderColor

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: page.contentInset
            spacing: 12

            RowLayout {
                Layout.fillWidth: true
                spacing: 12

                BreadcrumbBar {
                    id: breadcrumbBar
                    objectName: "breadcrumbBar"
                    Layout.fillWidth: true
                    Layout.preferredHeight: 36
                    path: root.breadcrumbPath
                    onPathClicked: function(folderId) { page.navigateToFolder(folderId) }
                }

                Label {
                    text: page.selectedItemIds.length > 1
                          ? page.selectedItemIds.length + " 已选中"
                          : (page.selectedItemId !== ""
                             ? "已选择：" + page.selectedItemName
                             : (page.currentFolderItemCount === 1 ? "1 项" : page.currentFolderItemCount + " 项"))
                    color: page.panelMutedTextColor
                    font.pixelSize: 12
                }
            }

            RowLayout {
                objectName: "driveSearchSortRow"
                Layout.fillWidth: true
                spacing: 8
                visible: page.isMyFilesMode

                TextField {
                    id: searchField
                    objectName: "driveSearchField"
                    Layout.fillWidth: true
                    Layout.maximumWidth: 240
                    placeholderText: "搜索文件..."
                    font.pixelSize: 13

                    onAccepted: {
                        page.searchQuery = text
                        page.submitSearch()
                    }

                    Connections {
                        target: page
                        ignoreUnknownSignals: true
                        function onSearchQueryChanged() {
                            if (page.searchQuery === "" && searchField.text !== "") {
                                searchField.text = ""
                            }
                        }
                    }
                }

                Button {
                    objectName: "driveSearchButton"
                    text: "搜索"
                    highlighted: true
                    onClicked: {
                        page.searchQuery = searchField.text
                        page.submitSearch()
                    }
                }

                Button {
                    objectName: "driveClearSearchButton"
                    text: "清除"
                    visible: page.isSearchActive
                    onClicked: page.clearSearch()
                }

                Item {
                    Layout.fillWidth: true
                }

                Label {
                    text: "排序："
                    color: page.panelMutedTextColor
                    font.pixelSize: 12
                }

                ComboBox {
                    id: sortCombo
                    objectName: "driveSortCombo"
                    Layout.preferredWidth: 160
                    model: [
                        { text: "名称 (A-Z)", value: "name_asc" },
                        { text: "名称 (Z-A)", value: "name_desc" },
                        { text: "最新优先", value: "updated_desc" },
                        { text: "最旧优先", value: "updated_asc" },
                        { text: "大小（大优先）", value: "size_desc" },
                        { text: "大小（小优先）", value: "size_asc" }
                    ]
                    textRole: "text"
                    valueRole: "value"
                    currentIndex: 0

                    onActivated: function(index) {
                        page.applySort(currentValue)
                    }

                    Connections {
                        target: page
                        ignoreUnknownSignals: true
                        function onCurrentSortChanged() {
                            for (var i = 0; i < sortCombo.count; ++i) {
                                if (sortCombo.model[i].value === page.currentSort) {
                                    sortCombo.currentIndex = i
                                    break
                                }
                            }
                        }
                    }
                }

                Button {
                    objectName: "driveViewToggle"
                    text: page.currentViewLayout === "list" ? "网格" : "列表"
                    onClicked: page.toggleViewLayout()
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: page.panelMutedFillColor
                radius: page.innerPanelRadius
                border.color: page.panelBorderColor

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 10
                    spacing: page.compactSpacing

                    Rectangle {
                        objectName: "fileTableHeaderSurface"
                        Layout.fillWidth: true
                        color: page.pageBackgroundColor
                        radius: page.innerPanelRadius
                        border.color: page.panelBorderColor
                        implicitHeight: fileTableHeaderRow.implicitHeight + 16
                        clip: true
                        visible: page.currentViewLayout === "list"

                        RowLayout {
                            id: fileTableHeaderRow
                            objectName: "fileTableHeaderRow"
                            anchors.fill: parent
                            anchors.leftMargin: 12
                            anchors.rightMargin: 12
                            spacing: page.tableColumnSpacing

                            Label {
                                objectName: "fileTableHeaderName"
                                Layout.fillWidth: true
                                Layout.minimumWidth: 0
                                text: "名称"
                                color: page.tableHeaderTextColor
                                font.pixelSize: 12
                                font.bold: true
                                elide: Text.ElideRight
                                wrapMode: Text.NoWrap
                                maximumLineCount: 1
                                verticalAlignment: Text.AlignVCenter
                            }

                            Label {
                                objectName: "fileTableHeaderType"
                                Layout.preferredWidth: page.fileTypeColumnWidth
                                Layout.minimumWidth: 0
                                text: "类型"
                                color: page.tableHeaderTextColor
                                font.pixelSize: 12
                                font.bold: true
                                elide: Text.ElideRight
                                wrapMode: Text.NoWrap
                                maximumLineCount: 1
                                verticalAlignment: Text.AlignVCenter
                            }

                            Label {
                                objectName: "fileTableHeaderSize"
                                Layout.preferredWidth: page.fileSizeColumnWidth
                                Layout.minimumWidth: 0
                                text: "大小"
                                color: page.tableHeaderTextColor
                                font.pixelSize: 12
                                font.bold: true
                                elide: Text.ElideRight
                                wrapMode: Text.NoWrap
                                maximumLineCount: 1
                                verticalAlignment: Text.AlignVCenter
                            }

                            Label {
                                objectName: "fileTableHeaderUpdated"
                                Layout.preferredWidth: page.fileUpdatedColumnWidth
                                Layout.minimumWidth: 0
                                text: "更新日期"
                                color: page.tableHeaderTextColor
                                font.pixelSize: 12
                                font.bold: true
                                elide: Text.ElideRight
                                wrapMode: Text.NoWrap
                                maximumLineCount: 1
                                verticalAlignment: Text.AlignVCenter
                            }

                            Item {
                                Layout.preferredWidth: page.fileActionColumnWidth
                            }
                        }
                    }

                    ListView {
                        id: fileListView
                        objectName: "fileListView"
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        model: driveManager.listModel
                        clip: true
                        spacing: 1

                        delegate: ItemDelegate {
                            id: fileRowDelegate
                            objectName: "fileRowDelegate_" + String(model.id)
                            width: ListView.view.width
                            implicitHeight: page.currentViewLayout === "list" ? page.fileRowHeight : 80
                            highlighted: page.isItemSelected(model.id)
                            hoverEnabled: true

                            onClicked: page.selectItem(model.id, model.kind, model.name)

                            onDoubleClicked: {
                                if (model.kind === "folder") {
                                    page.navigateToFolder(model.id)
                                }
                            }

                            TapHandler {
                                acceptedButtons: Qt.RightButton
                                onTapped: function(eventPoint, button) {
                                    if (!page.isItemSelected(String(model.id))) {
                                        page.selectItem(model.id, model.kind, model.name)
                                    }
                                    driveContextMenu.targetItemId = String(model.id)
                                    driveContextMenu.targetItemKind = String(model.kind)
                                    driveContextMenu.targetItemName = String(model.name)
                                    driveContextMenu.popup()
                                }
                            }

                            background: Rectangle {
                                radius: page.innerPanelRadius
                                color: fileRowDelegate.highlighted
                                       ? page.panelAccentFillColor
                                       : (fileRowDelegate.hovered ? page.panelBackgroundColor : "transparent")
                                border.color: fileRowDelegate.highlighted ? page.panelBorderColor : "transparent"
                            }

                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 12
                                anchors.rightMargin: 12
                                spacing: page.tableColumnSpacing

                                CheckBox {
                                    objectName: "fileCheckBox_" + String(model.id)
                                    checked: page.isItemSelected(model.id)
                                    onToggled: page.toggleItemSelection(model.id, model.kind, model.name)
                                }

                                Item {
                                    Layout.fillWidth: true
                                    Layout.minimumWidth: 0
                                    implicitHeight: fileNameRow.implicitHeight

                                    RowLayout {
                                        id: fileNameRow
                                        anchors.fill: parent
                                        spacing: 10

                                        Label {
                                            text: model.kind === "folder" ? "📁" : "📄"
                                            font.pixelSize: 18
                                        }

                                        Label {
                                            id: fileNameLabel
                                            objectName: "fileNameLabel_" + String(model.id)
                                            Layout.fillWidth: true
                                            Layout.minimumWidth: 0
                                            text: model.name
                                            font.pixelSize: 14
                                            font.weight: fileRowDelegate.highlighted ? Font.DemiBold : Font.Medium
                                            color: page.tableBodyPrimaryTextColor
                                            elide: Text.ElideRight
                                            wrapMode: Text.NoWrap
                                            maximumLineCount: 1
                                            verticalAlignment: Text.AlignVCenter
                                            ToolTip.visible: truncated && fileRowDelegate.hovered
                                            ToolTip.delay: 350
                                            ToolTip.timeout: 5000
                                            ToolTip.text: text
                                        }
                                    }
                                }

                                Label {
                                    objectName: "fileTypeLabel_" + String(model.id)
                                    Layout.preferredWidth: page.fileTypeColumnWidth
                                    Layout.minimumWidth: 0
                                    text: page.formatItemType(model.kind, model.mimeType)
                                    font.pixelSize: 13
                                    color: page.tableBodySecondaryTextColor
                                    elide: Text.ElideRight
                                    wrapMode: Text.NoWrap
                                    maximumLineCount: 1
                                    verticalAlignment: Text.AlignVCenter
                                    visible: page.currentViewLayout === "list"
                                }

                                Label {
                                    objectName: "fileSizeLabel_" + String(model.id)
                                    Layout.preferredWidth: page.fileSizeColumnWidth
                                    Layout.minimumWidth: 0
                                    text: page.formatItemSize(model.kind, model.size, model.itemCount)
                                    font.pixelSize: 13
                                    color: page.tableBodySecondaryTextColor
                                    elide: Text.ElideRight
                                    wrapMode: Text.NoWrap
                                    maximumLineCount: 1
                                    verticalAlignment: Text.AlignVCenter
                                    visible: page.currentViewLayout === "list"
                                }

                                Label {
                                    objectName: "fileUpdatedLabel_" + String(model.id)
                                    Layout.preferredWidth: page.fileUpdatedColumnWidth
                                    Layout.minimumWidth: 0
                                    text: page.formatUpdatedAtText(model.updatedAt)
                                    font.pixelSize: 13
                                    color: page.tableBodyTertiaryTextColor
                                    elide: Text.ElideRight
                                    wrapMode: Text.NoWrap
                                    maximumLineCount: 1
                                    verticalAlignment: Text.AlignVCenter
                                    visible: page.currentViewLayout === "list"
                                }

                                Button {
                                    Layout.preferredWidth: page.fileActionColumnWidth
                                    text: "打开"
                                    flat: true
                                    visible: model.kind === "folder"
                                    onClicked: page.navigateToFolder(model.id)
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    DriveContextMenu {
        id: driveContextMenu
        page: root.page
    }
}
