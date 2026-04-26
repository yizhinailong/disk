import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root
    
    // loading, empty, content, error, batchResult
    property string pageState: "loading"
    
    // Content to show in empty state
    property string emptyText: "No items found"
    property string emptyIcon: "qrc:/icons/empty.svg"
    
    // Content to show in error state
    property string errorText: "An error occurred"
    property string errorIcon: "qrc:/icons/error.svg"
    
    // Signals
    signal retryClicked()
    signal actionClicked()
    
    // Default content item (the actual page content)
    default property alias contentItem: contentContainer.data
    
    // Batch result item
    property Component batchResultComponent: null
    
    // State views
    
    // 1. Loading State
    Item {
        id: loadingView
        anchors.fill: parent
        visible: root.pageState === "loading"
        
        BusyIndicator {
            anchors.centerIn: parent
            running: loadingView.visible
        }
    }
    
    // 2. Empty State
    Item {
        id: emptyView
        anchors.fill: parent
        visible: root.pageState === "empty"
        
        ColumnLayout {
            anchors.centerIn: parent
            spacing: 16
            
            // Placeholder for icon
            Rectangle {
                Layout.alignment: Qt.AlignHCenter
                width: 64
                height: 64
                color: "transparent"
                border.color: "#ccc"
                radius: 32
                
                Text {
                    anchors.centerIn: parent
                    text: "!"
                    font.pixelSize: 24
                    color: "#ccc"
                }
            }
            
            Text {
                Layout.alignment: Qt.AlignHCenter
                text: root.emptyText
                font.pixelSize: 16
                color: "#666"
            }
            
            Button {
                Layout.alignment: Qt.AlignHCenter
                text: "Action"
                visible: false // Show only if needed
                onClicked: root.actionClicked()
            }
        }
    }
    
    // 3. Error State
    Item {
        id: errorView
        anchors.fill: parent
        visible: root.pageState === "error"
        
        ColumnLayout {
            anchors.centerIn: parent
            spacing: 16
            
            // Placeholder for icon
            Rectangle {
                Layout.alignment: Qt.AlignHCenter
                width: 64
                height: 64
                color: "transparent"
                border.color: "#f44336"
                radius: 32
                
                Text {
                    anchors.centerIn: parent
                    text: "X"
                    font.pixelSize: 24
                    color: "#f44336"
                }
            }
            
            Text {
                Layout.alignment: Qt.AlignHCenter
                text: root.errorText
                font.pixelSize: 16
                color: "#f44336"
            }
            
            Button {
                Layout.alignment: Qt.AlignHCenter
                text: "Retry"
                onClicked: root.retryClicked()
            }
        }
    }
    
    // 4. Content State
    Item {
        id: contentContainer
        anchors.fill: parent
        visible: root.pageState === "content"
    }
    
    // 5. Batch Result State
    Loader {
        id: batchResultLoader
        anchors.fill: parent
        visible: root.pageState === "batchResult"
        sourceComponent: root.batchResultComponent
        active: visible
    }
}
