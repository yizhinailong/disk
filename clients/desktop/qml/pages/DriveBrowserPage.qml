import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../components"

Page {
    id: root
    
    property string currentFolderId: "root"
    
    PageStateView {
        id: stateView
        anchors.fill: parent
        pageState: shellController.pageState
        
        emptyText: "This folder is empty"
        errorText: "Failed to load folder contents"
        
        onRetryClicked: {
            driveManager.listFiles(currentFolderId)
        }
        
        // Content state
        ColumnLayout {
            anchors.fill: parent
            
            RowLayout {
                Layout.fillWidth: true
                Layout.margins: 10
                
                BreadcrumbBar {
                    id: breadcrumbBar
                    Layout.fillWidth: true
                    // Will be populated by driveManager.breadcrumbLoaded signal
                }
                
                TextField {
                    id: searchField
                    placeholderText: "Search files..."
                    Layout.preferredWidth: 200
                    onAccepted: {
                        if (text.trim() !== "") {
                            driveManager.searchFiles(text)
                        } else {
                            driveManager.listFiles(currentFolderId)
                        }
                    }
                }
            }
            
            RowLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                
                FolderTreePanel {
                    Layout.preferredWidth: 200
                    Layout.fillHeight: true
                    model: driveManager.treeModel
                }
                
                // Main file list area
                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    color: "#f5f5f5"
                    
                    ListView {
                        id: fileListView
                        anchors.fill: parent
                        anchors.margins: 10
                        model: driveManager.listModel
                        clip: true
                        
                        delegate: ItemDelegate {
                            width: ListView.view.width
                            text: model.name
                            
                            onClicked: {
                                if (model.isDir) {
                                    currentFolderId = model.id
                                    driveManager.listFiles(currentFolderId)
                                    driveManager.loadBreadcrumb(currentFolderId)
                                } else {
                                    driveManager.getFileDetail(model.id)
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    
    Connections {
        target: driveManager
        function onBreadcrumbLoaded(breadcrumb) {
            breadcrumbBar.path = breadcrumb
        }
    }
    
    Component.onCompleted: {
        driveManager.loadFolderTree()
        driveManager.listFiles(currentFolderId)
        driveManager.loadBreadcrumb(currentFolderId)
    }
}
