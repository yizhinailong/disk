#include <QCoreApplication>
#include <QDebug>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QUrlQuery>

#include <api/ApiClient.hpp>
#include <api/AuthApi.hpp>
#include <api/FileApi.hpp>
#include <api/FolderApi.hpp>
#include <api/TrashApi.hpp>
#include <services/AuthService.hpp>
#include <services/FileService.hpp>
#include <services/FolderService.hpp>
#include <services/TokenRefreshCoordinator.hpp>
#include <services/TokenStore.hpp>
#include <services/TrashService.hpp>
#include <transfers/TransferStore.hpp>
#include <utils/ConfigStore.hpp>
#include <viewmodels/FileListViewModel.hpp>
#include <viewmodels/LoginViewModel.hpp>
#include <viewmodels/RegisterViewModel.hpp>
#include <viewmodels/SessionViewModel.hpp>
#include <viewmodels/SettingsViewModel.hpp>
#include <viewmodels/TransfersViewModel.hpp>
#include <viewmodels/TrashViewModel.hpp>

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

    // Inject stored JWT token into ApiClient on startup
    if (tokenStore.HasValidAccessToken()) {
        apiClient.SetBearerToken(tokenStore.AccessToken());
    }

    disk::qml::api::AuthApi authApi(&apiClient);
    disk::qml::api::FileApi fileApi(&apiClient);
    disk::qml::api::FolderApi folderApi(&apiClient);
    disk::qml::api::TrashApi trashApi(&apiClient);
    disk::qml::services::AuthService authService(&authApi, &tokenStore, &apiClient);

    disk::qml::services::TokenRefreshCoordinator tokenRefreshCoordinator(
        &authService,
        &tokenStore,
        &apiClient
    );
    disk::qml::services::FileService fileService(&fileApi, &tokenRefreshCoordinator);
    disk::qml::services::FolderService folderService(&folderApi, &tokenRefreshCoordinator);
    disk::qml::services::TrashService trashService(&trashApi, &tokenRefreshCoordinator);

    disk::qml::viewmodels::LoginViewModel loginViewModel(&authService);
    disk::qml::viewmodels::RegisterViewModel registerViewModel(&authService);

    disk::qml::viewmodels::SessionViewModel sessionViewModel(
        &loginViewModel,
        &tokenStore,
        &authService,
        &configStore
    );

    QObject::connect(
        &tokenRefreshCoordinator,
        &disk::qml::services::TokenRefreshCoordinator::forceLogout,
        &sessionViewModel,
        &disk::qml::viewmodels::SessionViewModel::logout
    );

    disk::qml::viewmodels::FileListViewModel fileListViewModel(
        &fileService,
        &folderService
    );

    disk::qml::viewmodels::SettingsViewModel settingsViewModel(
        &configStore,
        &apiClient
    );

    disk::qml::viewmodels::TrashViewModel trashViewModel(&trashService);

    disk::qml::transfers::TransferStore transferStore;

    disk::qml::viewmodels::TransfersViewModel transfersViewModel(
        &apiClient,
        &transferStore,
        &configStore
    );

    // --- QML engine setup ---
    disk::qml::viewmodels::LoginViewModel::SetInstance(&loginViewModel);
    disk::qml::viewmodels::RegisterViewModel::SetInstance(&registerViewModel);
    disk::qml::viewmodels::SessionViewModel::SetInstance(&sessionViewModel);
    disk::qml::viewmodels::FileListViewModel::SetInstance(&fileListViewModel);
    disk::qml::viewmodels::SettingsViewModel::SetInstance(&settingsViewModel);
    disk::qml::viewmodels::TrashViewModel::SetInstance(&trashViewModel);
    disk::qml::viewmodels::TransfersViewModel::SetInstance(&transfersViewModel);

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
