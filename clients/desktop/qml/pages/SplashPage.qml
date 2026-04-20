import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Page {
    id: root
    
    ColumnLayout {
        anchors.centerIn: parent
        spacing: 24
        
        Text {
            Layout.alignment: Qt.AlignHCenter
            text: "Disk Desktop"
            font.pixelSize: 32
            font.bold: true
        }
        
        BusyIndicator {
            Layout.alignment: Qt.AlignHCenter
            running: true
        }
        
        Text {
            Layout.alignment: Qt.AlignHCenter
            text: "Initializing..."
            color: "#666"
        }
    }
}
