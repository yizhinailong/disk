import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQml.Models

Item {
    id: root

    property var model: null
    property string currentFolderId: ""

    WorkspaceTheme { id: theme }

    readonly property int panelPadding: theme.panelInset
    readonly property int rowHeight: 38
    readonly property int rowRadius: theme.innerPanelRadius
    readonly property int rowSpacing: 2
    readonly property int rowIndentation: 18
    readonly property int disclosureWidth: 18
    readonly property int disclosureSpacing: theme.compactSpacing
    readonly property int rowHorizontalPadding: 10
    readonly property color titleTextColor: theme.strongTextColor
    readonly property color idleTextColor: theme.strongTextColor
    readonly property color mutedTextColor: theme.mutedTextColor
    readonly property color ancestorTextColor: theme.accentTextColor
    readonly property color hoverRowColor: theme.panelMutedFillColor
    readonly property color currentRowColor: "#eef4fb"
    readonly property color currentRowBorderColor: "#c7d6e6"
    readonly property color activeRowColor: theme.accentFillColor
    readonly property color activeRowBorderColor: "#9fb8d3"
    readonly property color activeTextColor: theme.accentTextColor
    readonly property color ancestorMarkerColor: "#c9d9ea"

    signal folderClicked(string folderId)

    function normalizeFolderId(folderId) {
        if (folderId === undefined || folderId === null) {
            return ""
        }

        return String(folderId)
    }

    function folderNumber(folderId) {
        var normalizedId = normalizeFolderId(folderId)
        if (normalizedId === "") {
            return -1
        }

        var numericId = Number(normalizedId)
        if (!isFinite(numericId) || numericId < 0) {
            return -1
        }

        return numericId
    }

    function scheduleCurrentFolderSync() {
        Qt.callLater(syncCurrentFolderState)
    }

    function syncCurrentFolderState() {
        if (!root.model || !folderTreeView.selectionModel) {
            return
        }

        var currentFolderNumber = root.folderNumber(root.currentFolderId)
        if (currentFolderNumber < 0) {
            return
        }

        var activePath = root.model.ancestorPath(currentFolderNumber)
        for (var pathIndex = 0; pathIndex < activePath.length - 1; ++pathIndex) {
            var ancestorIndex = root.model.indexOf(activePath[pathIndex])
            if (ancestorIndex.valid) {
                folderTreeView.expandToIndex(ancestorIndex)
            }
        }

        var currentIndex = root.model.indexOf(currentFolderNumber)
        if (!currentIndex.valid) {
            folderTreeView.selectionModel.clearSelection()
            folderTreeView.selectionModel.clearCurrentIndex()
            folderTreeView.forceLayout()
            return
        }

        folderTreeView.expandToIndex(currentIndex)
        folderTreeView.selectionModel.setCurrentIndex(currentIndex, ItemSelectionModel.NoUpdate)
        folderTreeView.forceLayout()

        var currentRow = folderTreeView.rowAtIndex(currentIndex)
        if (currentRow >= 0) {
            folderTreeView.positionViewAtRow(currentRow, Qt.AlignVCenter)
        }
    }

    function activateFolder(folderId, modelIndex) {
        var normalizedId = root.normalizeFolderId(folderId)
        if (normalizedId === "") {
            return
        }

        if (folderTreeView.selectionModel && modelIndex && modelIndex.valid) {
            folderTreeView.selectionModel.setCurrentIndex(modelIndex, ItemSelectionModel.NoUpdate)
        }

        root.folderClicked(normalizedId)
    }

    Component.onCompleted: root.scheduleCurrentFolderSync()
    onCurrentFolderIdChanged: root.scheduleCurrentFolderSync()
    onModelChanged: root.scheduleCurrentFolderSync()

    Connections {
        target: root.model
        ignoreUnknownSignals: true

        function onModelReset() {
            root.scheduleCurrentFolderSync()
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: root.panelPadding
        spacing: theme.panelSpacing

        Label {
            text: "Folders"
            color: root.titleTextColor
            font.pixelSize: 14
            font.bold: true
        }

        TreeView {
            id: folderTreeView
            objectName: "folderTreeView"
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: root.model
            boundsBehavior: Flickable.StopAtBounds
            pointerNavigationEnabled: false

            selectionModel: ItemSelectionModel {
                model: root.model
            }

            ScrollBar.vertical: ScrollBar {
                policy: ScrollBar.AsNeeded
            }

            delegate: Item {
                id: treeRow
                objectName: "folderRow_" + root.normalizeFolderId(model.id)

                required property TreeView treeView
                required property bool expanded
                required property bool hasChildren
                required property int row
                required property bool current
                required property int depth

                readonly property string folderId: root.normalizeFolderId(model.id)
                readonly property string folderName: model.name && String(model.name).length > 0 ? String(model.name) : "Folder"
                readonly property int folderDepth: model.depth !== undefined ? Number(model.depth) : depth
                readonly property bool activeFolder: folderId !== "" && folderId === root.currentFolderId
                readonly property bool ancestorFolder: folderId !== "" && root.folderNumber(root.currentFolderId) >= 0
                                                     && root.model && root.model.isAncestor(root.folderNumber(folderId), root.folderNumber(root.currentFolderId))
                readonly property color rowFillColor: activeFolder
                                                         ? root.activeRowColor
                                                         : (current ? root.currentRowColor
                                                                    : (rowMouseArea.containsMouse ? root.hoverRowColor : "transparent"))
                readonly property color rowBorderColor: activeFolder
                                                           ? root.activeRowBorderColor
                                                           : (current ? root.currentRowBorderColor : "transparent")
                readonly property bool rowBorderVisible: activeFolder || current
                readonly property color rowTextColor: activeFolder
                                                          ? root.activeTextColor
                                                          : (ancestorFolder ? root.ancestorTextColor : root.idleTextColor)
                readonly property bool labelTruncated: nameLabel.truncated
                readonly property bool showNameToolTip: labelTruncated && rowMouseArea.containsMouse
                readonly property bool labelClearsDisclosure: !disclosureContainer.visible
                                                               || nameLabel.x >= disclosureContainer.x + disclosureContainer.width + root.disclosureSpacing - 1

                width: treeView.width
                implicitHeight: root.rowHeight
                implicitWidth: treeView.width > 0 ? treeView.width : 1

                Rectangle {
                    anchors.fill: parent
                    radius: root.rowRadius
                    color: treeRow.rowFillColor
                    border.width: treeRow.rowBorderVisible ? 1 : 0
                    border.color: treeRow.rowBorderColor
                }

                Rectangle {
                    visible: treeRow.ancestorFolder && !treeRow.activeFolder
                    width: 3
                    radius: 2
                    color: root.ancestorMarkerColor
                    anchors.left: parent.left
                    anchors.leftMargin: 6
                    anchors.top: parent.top
                    anchors.bottom: parent.bottom
                    anchors.topMargin: 7
                    anchors.bottomMargin: 7
                }

                Item {
                    id: disclosureContainer
                    objectName: "folderDisclosure_" + treeRow.folderId
                    visible: treeRow.hasChildren
                    z: 1
                    width: root.disclosureWidth
                    height: parent.height
                    x: root.rowHorizontalPadding + treeRow.folderDepth * root.rowIndentation

                    Label {
                        anchors.centerIn: parent
                        text: treeRow.expanded ? "▾" : "▸"
                        color: root.mutedTextColor
                        font.pixelSize: 12
                        font.bold: true
                    }

                    MouseArea {
                        objectName: "folderDisclosureHitArea_" + treeRow.folderId
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: function(mouse) {
                            mouse.accepted = true
                            treeRow.treeView.toggleExpanded(treeRow.row)
                        }
                    }
                }

                Label {
                    id: nameLabel
                    objectName: "folderLabel_" + treeRow.folderId
                    x: root.rowHorizontalPadding + treeRow.folderDepth * root.rowIndentation + (treeRow.hasChildren ? root.disclosureWidth + root.disclosureSpacing : 0)
                    width: Math.max(0, parent.width - x - root.rowHorizontalPadding)
                    anchors.verticalCenter: parent.verticalCenter
                    text: treeRow.folderName
                    color: treeRow.rowTextColor
                    font.pixelSize: 13
                    font.bold: treeRow.activeFolder
                    font.weight: treeRow.activeFolder ? Font.DemiBold : Font.Medium
                    elide: Text.ElideRight
                    wrapMode: Text.NoWrap
                    maximumLineCount: 1
                    verticalAlignment: Text.AlignVCenter
                    clip: true

                    ToolTip.visible: treeRow.showNameToolTip
                    ToolTip.delay: 350
                    ToolTip.timeout: 5000
                    ToolTip.text: treeRow.folderName
                }

                MouseArea {
                    id: rowMouseArea
                    objectName: "folderActivationHitArea_" + treeRow.folderId
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: function(mouse) {
                        mouse.accepted = true
                        root.activateFolder(treeRow.folderId, treeRow.treeView.index(treeRow.row, 0))
                    }
                }
            }
        }
    }
}
