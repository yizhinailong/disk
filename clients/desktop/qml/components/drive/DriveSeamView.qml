import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root

    required property var page

    objectName: "viewModeSeamContainer"
    visible: !page.isMyFilesMode && !page.isSharedMode && !page.isTrashMode
    color: page.panelBackgroundColor
    radius: page.panelRadius
    border.color: page.panelBorderColor

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 40
        spacing: page.panelSpacing

        Item {
            Layout.fillHeight: true
        }

        Label {
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignHCenter
            text: page.viewModeTitleText()
            color: page.panelStrongTextColor
            font.pixelSize: 20
            font.bold: true
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
        }

        Label {
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignHCenter
            text: page.viewModeStatusText()
            color: page.panelMutedTextColor
            font.pixelSize: 14
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
        }

        Label {
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignHCenter
            text: page.seamDescription()
            color: page.panelTertiaryTextColor
            font.pixelSize: 13
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
        }

        Item {
            Layout.fillHeight: true
        }
    }
}
