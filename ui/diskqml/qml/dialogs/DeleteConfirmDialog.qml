/**
 * @file DeleteConfirmDialog.qml
 * @brief 删除确认对话框
 * @details 确认删除文件的对话框，支持单个和批量删除，删除后移入回收站
 */
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Disk 1.0

Dialog {
    id: dlg
    title: "⚠️ 确认删除"
    modal: true
    width: 400
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

    ///< 待删除的文件ID列表
    property var targetFileIds: []
    ///< 显示名称（单个文件删除时使用）
    property string targetFileName: ""

    function openForFiles(fileIds: var, displayName: string) {
        targetFileIds = fileIds
        targetFileName = displayName
        dlg.open()
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: StyleTokens.spacingMd

        Label {
            text: {
                if (dlg.targetFileIds.length === 1) {
                    return "确定要删除 \"" + dlg.targetFileName + "\" 吗？"
                }
                return "确定要删除选中的 " + dlg.targetFileIds.length + " 个项目吗？"
            }
            font.pixelSize: StyleTokens.fontSizeBody
            font.weight: StyleTokens.fontWeightBody
            color: StyleTokens.colorTextPrimary
            wrapMode: Text.Wrap
            Layout.fillWidth: true
        }

        Label {
            text: "此操作将移入回收站，可在 30 天内恢复。"
            font.pixelSize: StyleTokens.fontSizeSmall
            font.weight: StyleTokens.fontWeightSmall
            color: StyleTokens.colorTextSecondary
            Layout.fillWidth: true
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
                text: "确认删除"
                onClicked: dlg.accept()
                
                background: Rectangle {
                    implicitHeight: 36
                    implicitWidth: 80
                    color: parent.down ? Qt.darker(StyleTokens.colorError, 1.1) : (parent.hovered ? Qt.lighter(StyleTokens.colorError, 1.1) : StyleTokens.colorError)
                    radius: StyleTokens.radiusSmall
                }
                contentItem: Text {
                    text: parent.text
                    font.pixelSize: StyleTokens.fontSizeBody
                    color: StyleTokens.colorSurface
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
            }
        }
    }

    onAccepted: {
        FileListViewModel.deleteFiles(dlg.targetFileIds)
    }
}
