import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Dialog {
    id: root
    modal: true
    width: 360
    standardButtons: Dialog.NoButton
    closePolicy: Popup.NoAutoClose

    property alias message: messageLabel.text

    signal confirmed()
    signal cancelled()

    ColumnLayout {
        width: parent.width
        spacing: 16

        Label {
            id: messageLabel
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
        }

        RowLayout {
            Layout.fillWidth: true

            Item {
                Layout.fillWidth: true
            }

            Button {
                text: "取消"
                onClicked: {
                    root.cancelled()
                    root.close()
                }
            }

            Button {
                text: "确认"
                highlighted: true
                onClicked: {
                    root.confirmed()
                    root.close()
                }
            }
        }
    }
}
