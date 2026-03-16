/**
 * @file NewFolderDialog.qml
 * @brief 新建文件夹对话框
 * @details 输入文件夹名称后创建新文件夹的对话框
 */
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Disk 1.0

Dialog {
    id: dlg
    title: "📝 新建文件夹"
    modal: true
    width: 450
    anchors.centerIn: parent
    standardButtons: Dialog.NoButton
    padding: StyleTokens.spacingLg

    background: Rectangle {
        color: StyleTokens.colorSurface
        radius: StyleTokens.radiusXl
        border.color: StyleTokens.colorBorder
        border.width: 1
    }

    Overlay.modal: Rectangle {
        color: Qt.rgba(0, 0, 0, 0.45)
    }

    onOpened: {
        nameField.text = ""
        nameField.forceActiveFocus()
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: StyleTokens.spacingMd

        Label {
            text: "文件夹名称"
            font.pixelSize: StyleTokens.fontSizeBody
            font.weight: StyleTokens.fontWeightBody
            color: StyleTokens.colorTextPrimary
        }

        TextField {
            id: nameField
            Layout.fillWidth: true
            placeholderText: "请输入文件夹名称"
            font.pixelSize: StyleTokens.fontSizeBody
            color: StyleTokens.colorTextPrimary
            selectByMouse: true
            
            background: Rectangle {
                implicitHeight: 40
                color: nameField.activeFocus ? StyleTokens.colorSurface : StyleTokens.colorBackground
                border.color: nameField.activeFocus ? StyleTokens.colorPrimary : "transparent"
                border.width: 1
                radius: StyleTokens.radiusMedium
            }

            Keys.onReturnPressed: {
                if (nameField.text.trim().length > 0)
                    dlg.accept()
            }
        }

        // --- 按钮行 ---
        RowLayout {
            Layout.fillWidth: true
            spacing: StyleTokens.spacingSm

            Item { Layout.fillWidth: true }

            Button {
                text: "取消"
                onClicked: dlg.reject()
                
                background: Rectangle {
                    implicitHeight: 36
                    implicitWidth: 80
                    color: parent.down ? StyleTokens.colorHover : "transparent"
                    border.color: StyleTokens.colorBorder
                    border.width: 1
                    radius: StyleTokens.radiusSmall
                }
                contentItem: Text {
                    text: parent.text
                    font.pixelSize: StyleTokens.fontSizeBody
                    color: StyleTokens.colorTextPrimary
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
            }

            Button {
                text: "确认"
                enabled: nameField.text.trim().length > 0
                onClicked: dlg.accept()
                
                background: Rectangle {
                    implicitHeight: 36
                    implicitWidth: 80
                    color: !parent.enabled ? StyleTokens.colorBackground : (parent.down ? StyleTokens.colorPrimaryHover : (parent.hovered ? Qt.lighter(StyleTokens.colorPrimary, 1.1) : StyleTokens.colorPrimary))
                    radius: StyleTokens.radiusSmall
                }
                contentItem: Text {
                    text: parent.text
                    font.pixelSize: StyleTokens.fontSizeBody
                    color: !parent.enabled ? StyleTokens.colorTextTertiary : "#FFFFFF"
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
            }
        }
    }

    onAccepted: {
        var name = nameField.text.trim()
        if (name.length > 0) {
            FileListViewModel.createFolder(name)
        }
    }
}
