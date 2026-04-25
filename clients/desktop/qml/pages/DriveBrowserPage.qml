import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../components"

Page {
    id: root
    
    property string currentFolderId: "0"

    function refreshCurrentFolder() {
        shellController.setPageState("loading")
        driveManager.loadFolderTree()
        driveManager.listFiles(currentFolderId)
        driveManager.loadBreadcrumb(currentFolderId)
    }

    function openFolder(folderId) {
        currentFolderId = String(folderId)
        shellController.setPageState("loading")
        driveManager.listFiles(currentFolderId)
        driveManager.loadBreadcrumb(currentFolderId)
    }
    
    PageStateView {
        id: stateView
        anchors.fill: parent
        pageState: shellController.pageState
        
        emptyText: "This folder is empty"
        errorText: "Failed to load folder contents"
        
        onRetryClicked: {
            root.refreshCurrentFolder()
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
                            shellController.setPageState("loading")
                            driveManager.searchFiles(text.trim())
                        } else {
                            root.refreshCurrentFolder()
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
                                if (model.kind === "folder") {
                                    root.openFolder(model.id)
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

        function onPaginationLoaded(page, totalPages, total) {
            shellController.setPageState(total > 0 ? "content" : "empty")
        }

        function onListLoadFailed(message, code) {
            shellController.setPageState("error")
        }
    }
    
    Component.onCompleted: {
        root.refreshCurrentFolder()
    }
}
