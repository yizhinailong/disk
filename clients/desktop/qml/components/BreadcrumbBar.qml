import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root
    
    property var path: [] // Array of objects { id: "...", name: "..." }
    
    signal pathClicked(string folderId)
    
    RowLayout {
        anchors.fill: parent
        spacing: 4
        
        Repeater {
            model: root.path
            
            delegate: RowLayout {
                spacing: 4
                
                Button {
                    text: modelData.name
                    flat: true
                    onClicked: root.pathClicked(modelData.id)
                }
                
                Text {
                    text: ">"
                    color: "#999"
                    visible: index < root.path.length - 1
                }
            }
        }
        
        Item {
            Layout.fillWidth: true
        }
    }
}
