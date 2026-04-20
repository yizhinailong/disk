import QtQml
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "shells"
import "pages"

Item {
    id: root

    property var currentWindow: null

    readonly property Component selectedShellComponent: {
        switch (shellController.currentShell) {
            case "splash": return splashComponent;
            case "login": return loginComponent;
            case "owner": return ownerComponent;
            case "visitor": return visitorComponent;
            default: return splashComponent;
        }
    }

    function presentShell() {
        const nextComponent = selectedShellComponent;
        if (!nextComponent) {
            return;
        }

        const previousWindow = currentWindow;
        const nextWindow = nextComponent.createObject(null);
        if (!nextWindow) {
            console.error("Failed to create shell window for", shellController.currentShell);
            return;
        }

        currentWindow = nextWindow;
        currentWindow.visible = true;

        if (currentWindow.raise) {
            currentWindow.raise();
        }

        if (currentWindow.requestActivate) {
            currentWindow.requestActivate();
        }

        if (previousWindow) {
            previousWindow.close();
            previousWindow.destroy();
        }
    }

    Component.onCompleted: presentShell()

    Connections {
        target: shellController

        function onCurrentShellChanged() {
            root.presentShell()
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
