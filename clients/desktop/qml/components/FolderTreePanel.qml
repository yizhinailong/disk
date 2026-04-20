import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root
    
    property var model: null
    
    signal folderClicked(string folderId)
    
    ColumnLayout {
        anchors.fill: parent
        
        Text {
            text: "Folders"
            font.bold: true
            Layout.margins: 8
        }
        
        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            
            ListView {
                id: treeView
                model: root.model
                
                delegate: ItemDelegate {
                    width: ListView.view.width
                    text: model.name || "Folder"
                    
                    // Indentation based on depth
                    leftPadding: 16 + (model.depth || 0) * 16
                    
                    onClicked: {
                        root.folderClicked(model.id || "")
                    }
                }
            }
        }
    }
}
