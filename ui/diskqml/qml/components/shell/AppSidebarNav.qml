import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../../tokens"

Rectangle {
    id: root
    color: StyleTokens.colorSurface

    property string layoutMode: "expanded"
    property bool showSidebarLabels: layoutMode !== "compact"
    property bool isTransferMode: false
    property string currentNav: "home"
    property var activeNavItems: []

    signal navClicked(string key)
    signal userProfileClicked()
    signal modeSwitchClicked()

    // 右侧分隔线
    Rectangle {
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: 1
        color: StyleTokens.colorBorder
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: StyleTokens.spacingSm
        spacing: StyleTokens.spacingXs

        // 导航项
        Repeater {
            model: root.activeNavItems

            delegate: ItemDelegate {
                id: navDelegate
                Layout.fillWidth: true
                height: 40
                highlighted: root.currentNav === modelData.key
                onClicked: root.navClicked(modelData.key)

                contentItem: RowLayout {
                    spacing: root.showSidebarLabels ? StyleTokens.spacingSm : 0

                    Label {
                        text: modelData.icon
                        font.pixelSize: StyleTokens.fontSizeH2
                        Layout.preferredWidth: 24
                        Layout.alignment: Qt.AlignHCenter
                        horizontalAlignment: Text.AlignHCenter
                    }

                    Label {
                        text: modelData.label
                        font.pixelSize: StyleTokens.fontSizeBody
                        color: navDelegate.highlighted
                               ? StyleTokens.colorPrimary
                               : StyleTokens.colorTextPrimary
                        visible: root.showSidebarLabels
                        Layout.fillWidth: true
                        elide: Text.ElideRight
                    }
                }

                ToolTip.visible: !root.showSidebarLabels && navDelegate.hovered
                ToolTip.text: modelData.label
                ToolTip.delay: 500

                background: Rectangle {
                    radius: StyleTokens.radiusMedium
                    color: navDelegate.highlighted
                           ? StyleTokens.colorPrimaryLight
                           : navDelegate.hovered ? StyleTokens.colorHover : "transparent"
                }
            }
        }

        // 弹簧
        Item { Layout.fillHeight: true }

        // 个人设置按钮
        ItemDelegate {
            id: userProfileBtn
            Layout.fillWidth: true
            height: 40
            highlighted: root.currentNav === "user"

            contentItem: RowLayout {
                spacing: root.showSidebarLabels ? StyleTokens.spacingSm : 0

                Label {
                    text: "👤"
                    font.pixelSize: StyleTokens.fontSizeH2
                    Layout.preferredWidth: 24
                    Layout.alignment: Qt.AlignHCenter
                    horizontalAlignment: Text.AlignHCenter
                }

                Label {
                    text: "个人设置"
                    font.pixelSize: StyleTokens.fontSizeBody
                    color: userProfileBtn.highlighted
                           ? StyleTokens.colorPrimary
                           : StyleTokens.colorTextPrimary
                    visible: root.showSidebarLabels
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                }
            }

            ToolTip.visible: !root.showSidebarLabels && userProfileBtn.hovered
            ToolTip.text: qsTr("个人设置")
            ToolTip.delay: 500

            background: Rectangle {
                radius: StyleTokens.radiusMedium
                color: userProfileBtn.highlighted
                       ? StyleTokens.colorPrimaryLight
                       : userProfileBtn.hovered ? StyleTokens.colorHover : "transparent"
            }

            onClicked: root.userProfileClicked()
        }

        // 模式切换按钮
        ItemDelegate {
            id: modeSwitchBtn
            Layout.fillWidth: true
            height: 40

            contentItem: RowLayout {
                spacing: root.showSidebarLabels ? StyleTokens.spacingSm : 0

                Label {
                    text: root.isTransferMode ? "📁" : "📤"
                    font.pixelSize: StyleTokens.fontSizeH2
                    Layout.preferredWidth: 24
                    Layout.alignment: Qt.AlignHCenter
                    horizontalAlignment: Text.AlignHCenter
                }

                Label {
                    text: root.isTransferMode ? "文件" : "传输"
                    font.pixelSize: StyleTokens.fontSizeBody
                    color: StyleTokens.colorTextPrimary
                    visible: root.showSidebarLabels
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                }
            }

            ToolTip.visible: !root.showSidebarLabels && modeSwitchBtn.hovered
            ToolTip.text: root.isTransferMode ? qsTr("切换到文件模式") : qsTr("切换到传输模式")
            ToolTip.delay: 500

            background: Rectangle {
                radius: StyleTokens.radiusMedium
                color: modeSwitchBtn.hovered ? StyleTokens.colorHover : "transparent"
            }

            onClicked: root.modeSwitchClicked()
        }
    }
}
