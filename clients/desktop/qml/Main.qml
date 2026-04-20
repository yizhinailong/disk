import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "shells"
import "pages"

Item {
    id: root
    
    Loader {
        id: shellLoader
        
        sourceComponent: {
            switch (shellController.currentShell) {
                case "splash": return splashComponent;
                case "login": return loginComponent;
                case "owner": return ownerComponent;
                case "visitor": return visitorComponent;
                default: return splashComponent;
            }
        }
    }
    
    Component {
        id: splashComponent
        ApplicationWindow {
            visible: true
            width: 1024
            height: 768
            title: "Disk Desktop"
            
            SplashPage {
                anchors.fill: parent
            }
        }
    }
    
    Component {
        id: loginComponent
        ApplicationWindow {
            visible: true
            width: 1024
            height: 768
            title: "Disk Desktop - Login"
            
            LoginPage {
                anchors.fill: parent
            }
        }
    }
    
    Component {
        id: ownerComponent
        OwnerShell {}
    }
    
    Component {
        id: visitorComponent
        VisitorShell {}
    }
}
