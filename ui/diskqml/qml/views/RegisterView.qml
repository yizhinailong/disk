import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root


    property string serverUrl: "http://127.0.0.1:8080"
    
    // Default HTTP client implementation
    property var defaultHttpClient: ({
        register: function(data, callback) {
            var xhr = new XMLHttpRequest();
            xhr.open("POST", serverUrl + "/api/auth/register");
            xhr.setRequestHeader("Content-Type", "application/json");
            xhr.onreadystatechange = function() {
                if (xhr.readyState === XMLHttpRequest.DONE) {
                    if (xhr.status === 0) {
                        callback({ok: false, error: "network"});
                    } else {
                        try {
                            var response = JSON.parse(xhr.responseText);
                            callback({ok: response.code === 0, code: response.code, message: response.message, data: response.data});
                        } catch(e) {
                            callback({ok: false, error: "parse"});
                        }
                    }
                }
            };
            xhr.send(JSON.stringify({username: data.username, email: data.email, password: data.password}));
        }
    })
    
    property var httpClient: defaultHttpClient
    
    signal registered(string username, string email)
    signal backRequested()

    property bool loading: false
    property string globalError: ""
    property bool showPassword: false

    // Validation patterns
    readonly property var usernameRegex: /^[a-zA-Z0-9_]{4,32}$/
    readonly property var emailRegex: /^[a-zA-Z0-9._%+-]+@[a-zA-Z0-9.-]+\.[a-zA-Z]{2,}$/
    readonly property var passwordRegex: /^(?=.*[a-z])(?=.*[A-Z])(?=.*\d)[a-zA-Z\d]{8,64}$/

    function isUsernameValid() { return usernameRegex.test(usernameInput.text); }
    function isEmailValid() { return emailRegex.test(emailInput.text); }
    function isPasswordValid() { return passwordRegex.test(passwordInput.text); }
    function isConfirmPasswordValid() { return passwordInput.text === confirmPasswordInput.text && passwordInput.text.length > 0; }
    function isFormValid() {
        return isUsernameValid() && isEmailValid() && isPasswordValid() && isConfirmPasswordValid();
    }

    Rectangle {
        anchors.fill: parent
        color: "#FAFAFA"
        
        ScrollView {
            anchors.fill: parent
            contentWidth: availableWidth
            clip: true
            
            ColumnLayout {
                width: Math.min(400, parent.width * 0.9)
                anchors.horizontalCenter: parent.horizontalCenter
                spacing: 16
                
                Item { Layout.preferredHeight: 32 } // Spacer
                
                // Back button
                Button {
                    text: "← 返回"
                    flat: true
                    Layout.alignment: Qt.AlignLeft
                    onClicked: root.backRequested()
                }

                Label {
                    text: "创建新账号"
                    font.pixelSize: 24
                    font.bold: true
                    color: "#212121"
                    Layout.alignment: Qt.AlignHCenter
                }

                Item { Layout.preferredHeight: 16 } // Spacer
                
                // Global Error Message
                Label {
                    id: globalErrorLabel
                    objectName: "globalErrorLabel"
                    visible: root.globalError !== ""
                    text: root.globalError
                    color: "#F44336"
                    font.pixelSize: 14
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.Wrap
                }

                // Username
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 4
                    Label {
                        text: "用户名"
                        color: "#212121"
                        font.pixelSize: 14
                    }
                    TextField {
                        id: usernameInput
                        objectName: "usernameInput"
                        Layout.fillWidth: true
                        placeholderText: "4-32个字符，仅包含字母、数字、下划线"
                        maximumLength: 32
                        enabled: !root.loading
                        onTextChanged: root.globalError = ""
                    }
                    Label {
                        objectName: "usernameErrorLabel"
                        text: "需为4-32个字符，仅支持字母、数字、下划线"
                        color: "#F44336"
                        font.pixelSize: 12
                        visible: usernameInput.text.length > 0 && !isUsernameValid()
                    }
                }

                // Email
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 4
                    Label {
                        text: "邮箱"
                        color: "#212121"
                        font.pixelSize: 14
                    }
                    TextField {
                        id: emailInput
                        objectName: "emailInput"
                        Layout.fillWidth: true
                        placeholderText: "请输入有效邮箱地址"
                        maximumLength: 254
                        enabled: !root.loading
                        onTextChanged: root.globalError = ""
                    }
                    Label {
                        objectName: "emailErrorLabel"
                        text: "请输入有效的邮箱格式"
                        color: "#F44336"
                        font.pixelSize: 12
                        visible: emailInput.text.length > 0 && !isEmailValid()
                    }
                }

                // Password
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 4
                    Label {
                        text: "密码"
                        color: "#212121"
                        font.pixelSize: 14
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 4
                        TextField {
                            id: passwordInput
                            objectName: "passwordInput"
                            Layout.fillWidth: true
                            placeholderText: "设置密码"
                            echoMode: root.showPassword ? TextInput.Normal : TextInput.Password
                            maximumLength: 64
                            enabled: !root.loading
                            onTextChanged: root.globalError = ""
                        }
                        Button {
                            id: showPasswordButton
                            objectName: "showPasswordButton"
                            text: root.showPassword ? "隐藏" : "显示"
                            onClicked: root.showPassword = !root.showPassword
                            enabled: !root.loading
                        }
                    }
                    Label {
                        objectName: "passwordErrorLabel"
                        text: "8-64个字符，必须同时包含大小写字母和数字，仅支持字母和数字"
                        color: "#F44336"
                        font.pixelSize: 12
                        visible: passwordInput.text.length > 0 && !isPasswordValid()
                        wrapMode: Text.Wrap
                        Layout.fillWidth: true
                    }
                }

                // Confirm Password
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 4
                    Label {
                        text: "确认密码"
                        color: "#212121"
                        font.pixelSize: 14
                    }
                    TextField {
                        id: confirmPasswordInput
                        objectName: "confirmPasswordInput"
                        Layout.fillWidth: true
                        placeholderText: "请再次输入密码"
                        echoMode: root.showPassword ? TextInput.Normal : TextInput.Password
                        maximumLength: 64
                        enabled: !root.loading
                        onTextChanged: root.globalError = ""
                    }
                    Label {
                        objectName: "confirmPasswordErrorLabel"
                        text: "两次输入的密码不一致"
                        color: "#F44336"
                        font.pixelSize: 12
                        visible: confirmPasswordInput.text.length > 0 && confirmPasswordInput.text !== passwordInput.text
                    }
                }

                Item { Layout.preferredHeight: 16 } // Spacer

                // Register Button
                Button {
                    id: submitButton
                    objectName: "submitButton"
                    Layout.fillWidth: true
                    Layout.preferredHeight: 40
                    text: root.loading ? "注册中..." : "注册"
                    enabled: isFormValid() && !root.loading
                    
                    background: Rectangle {
                        color: submitButton.enabled ? "#2196F3" : "#E0E0E0"
                        radius: 8
                    }
                    contentItem: Text {
                        text: submitButton.text
                        color: submitButton.enabled ? "#FFFFFF" : "#757575"
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                        font.pixelSize: 14
                        font.bold: true
                    }
                    
                    onClicked: {
                        root.globalError = "";
                        root.loading = true;
                        
                        var reqData = {
                            username: usernameInput.text,
                            email: emailInput.text,
                            password: passwordInput.text
                        };
                        
                        httpClient.register(reqData, function(response) {
                            root.loading = false;
                            
                            if (response.ok) {
                                // DO NOT store password here!
                                root.registered(usernameInput.text, emailInput.text);
                                return;
                            }
                            
                            if (response.error === "network") {
                                root.globalError = "网络连接失败，请检查网络";
                            } else if (response.error === "parse") {
                                root.globalError = "服务器响应解析失败";
                            } else {
                                // Error mapping
                                switch(response.code) {
                                    case 40001:
                                        root.globalError = "用户名已被注册";
                                        break;
                                    case 40002:
                                        root.globalError = "邮箱已被注册";
                                        break;
                                    case 10002:
                                    case 40003:
                                        root.globalError = "参数格式不正确";
                                        break;
                                    case 10005:
                                        root.globalError = "请求过于频繁，请稍后再试";
                                        break;
                                    case 10006:
                                        root.globalError = "服务器错误，请稍后重试";
                                        break;
                                    default:
                                        root.globalError = response.message || "未知错误";
                                        break;
                                }
                            }
                        });
                    }
                }
                
                Item { Layout.preferredHeight: 32 } // Bottom spacer
            }
        }
    }
}