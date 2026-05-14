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
            case "login": return authShellComponent;
            case "register": return authShellComponent;
            case "owner": return ownerComponent;
            case "visitor": return visitorComponent;
            default: return splashComponent;
        }
    }

    function presentShell() {
        const nextShell = shellController.currentShell;

        // Auth-to-auth transition: reuse existing auth window by switching mode
        const isAuthTarget = (nextShell === "login" || nextShell === "register");
        if (isAuthTarget && currentWindow !== null && currentWindow.authMode !== undefined) {
            currentWindow.authMode = nextShell;
            return;
        }

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

        // Set initial auth mode for auth shell
        if (isAuthTarget && nextWindow.authMode !== undefined) {
            nextWindow.authMode = nextShell;
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
            title: "Disk 桌面端"

            SplashPage {
                anchors.fill: parent
            }
        }
    }

    Component {
        id: authShellComponent
        AuthShell {}
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
