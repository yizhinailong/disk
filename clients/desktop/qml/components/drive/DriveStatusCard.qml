import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root

    required property var page

    color: page.panelBackgroundColor
    radius: page.panelRadius
    border.color: page.panelBorderColor
    implicitHeight: driveStatusRow.implicitHeight + 32

    RowLayout {
        id: driveStatusRow
        anchors.fill: parent
        anchors.margins: page.pagePadding
        spacing: page.panelSpacing

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 4

            Label {
                text: page.viewModeLabel()
                color: page.panelMutedTextColor
                font.pixelSize: 11
                font.bold: true
            }

            Label {
                text: page.viewModeTitleText()
                color: page.panelStrongTextColor
                font.pixelSize: 24
                font.bold: true
            }

            Label {
                Layout.fillWidth: true
                text: page.viewModeStatusText()
                color: page.panelMutedTextColor
                wrapMode: Text.WordWrap
            }
        }

        ColumnLayout {
            spacing: page.compactSpacing

            Rectangle {
                Layout.alignment: Qt.AlignRight
                visible: page.isMyFilesMode
                implicitWidth: stateChipLabel.implicitWidth + 20
                implicitHeight: stateChipLabel.implicitHeight + 10
                radius: implicitHeight / 2
                color: page.panelAccentFillColor

                Label {
                    id: stateChipLabel
                    anchors.centerIn: parent
                    text: page.pageStateLabel()
                    color: page.panelAccentTextColor
                    font.pixelSize: 11
                    font.bold: true
                }
            }

            Label {
                Layout.alignment: Qt.AlignRight
                visible: page.isMyFilesMode
                text: page.folderScopeLabel()
                color: page.panelMutedTextColor
                font.pixelSize: 12
            }
        }
    }
}
