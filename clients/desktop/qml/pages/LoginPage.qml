import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Page {
    id: root
    
    ColumnLayout {
        anchors.centerIn: parent
        width: 300
        spacing: 16
        
        Text {
            Layout.alignment: Qt.AlignHCenter
            text: "Login"
            font.pixelSize: 24
            font.bold: true
        }
        
        TextField {
            id: usernameField
            Layout.fillWidth: true
            placeholderText: "Username"
        }
        
        TextField {
            id: passwordField
            Layout.fillWidth: true
            placeholderText: "Password"
            echoMode: TextInput.Password
        }
        
        Button {
            Layout.fillWidth: true
            text: "Login"
            onClicked: {
                // In a real app, this would call authService.Login
                // For now, just simulate success by navigating to owner shell
                shellController.navigateToOwner()
            }
        }
        
        Button {
            Layout.fillWidth: true
            text: "Register"
            flat: true
        }
    }
}
