#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickStyle>

#include <api/ApiClient.hpp>
#include <api/AuthApi.hpp>
#include <app/AppContext.hpp>
#include <services/AuthService.hpp>
#include <services/TokenStore.hpp>
#include <utils/ConfigStore.hpp>
#include <viewmodels/LoginViewModel.hpp>
#include <viewmodels/RegisterViewModel.hpp>

int main(int argc, char* argv[]) {
    QGuiApplication app(argc, argv);
    QQuickStyle::setStyle("Material");

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

    disk::qml::app::AppContext appContext(
        &configStore,
        &tokenStore,
        &apiClient,
        &authApi,
        &authService,
        &loginViewModel,
        &registerViewModel
    );

    // --- QML engine setup ---
    disk::qml::app::AppContext::SetInstance(&appContext);

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
