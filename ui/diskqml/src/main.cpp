#include <QDebug>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickStyle>

#include <api/ApiClient.hpp>
#include <api/AuthApi.hpp>
#include <services/AuthService.hpp>
#include <services/TokenStore.hpp>
#include <utils/ConfigStore.hpp>
#include <viewmodels/LoginViewModel.hpp>
#include <viewmodels/RegisterViewModel.hpp>
#include <viewmodels/SessionViewModel.hpp>


int main(int argc, char* argv[]) {
    QGuiApplication app(argc, argv);
    QQuickStyle::setStyle("Fusion");

    QCoreApplication::setOrganizationName("Disk");
    QCoreApplication::setApplicationName("diskqml");

    // --- Dependency construction (order matters) ---
    disk::qml::utils::ConfigStore configStore;
    disk::qml::services::TokenStore tokenStore;

    disk::qml::api::ApiClient apiClient;
    apiClient.SetBaseUrl(configStore.ServerUrl());

    disk::qml::api::AuthApi authApi(&apiClient);
    disk::qml::services::AuthService authService(&authApi, &tokenStore);

    disk::qml::viewmodels::LoginViewModel loginViewModel(&authService);
    disk::qml::viewmodels::RegisterViewModel registerViewModel(&authService);

    disk::qml::viewmodels::SessionViewModel sessionViewModel(
        &loginViewModel,
        &tokenStore,
        &authService,
        &configStore
    );

    // --- QML engine setup ---
    disk::qml::viewmodels::LoginViewModel::SetInstance(&loginViewModel);
    disk::qml::viewmodels::RegisterViewModel::SetInstance(&registerViewModel);
    disk::qml::viewmodels::SessionViewModel::SetInstance(&sessionViewModel);

    QQmlApplicationEngine engine;
    QObject::connect(
        &engine,
        &QQmlApplicationEngine::objectCreationFailed,
        &app,
        []() { QCoreApplication::exit(-1); },
        Qt::QueuedConnection
    );
    engine.loadFromModule("Disk", "Main");

    return app.exec();
}
