#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QDebug>
#include <QCoreApplication>
#include <QEventLoop>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QTimer>
#include <QUrlQuery>
#include <api/ApiClient.hpp>
#include <api/AuthApi.hpp>
#include <api/FileApi.hpp>
#include <api/FolderApi.hpp>
#include <api/TrashApi.hpp>
#include <services/AuthService.hpp>
#include <services/FileService.hpp>
#include <services/FolderService.hpp>
#include <services/TrashService.hpp>
#include <services/TokenStore.hpp>
#include <utils/ConfigStore.hpp>
#include <viewmodels/FileListViewModel.hpp>
#include <viewmodels/TrashViewModel.hpp>
#include <viewmodels/LoginViewModel.hpp>
#include <viewmodels/RegisterViewModel.hpp>
#include <viewmodels/SessionViewModel.hpp>
#include <viewmodels/SettingsViewModel.hpp>
#include <transfers/TransferStore.hpp>
#include <viewmodels/TransfersViewModel.hpp>


/**
 * @brief Run headless auth smoke test.
 * @details Verifies authentication by making two API calls:
 *   1. GET /api/file/list?parent_id=0&page=1&page_size=1&sort_by=name&sort_order=asc&type=all
 *   2. GET /api/folder/0/breadcrumb
 * Exits 0 if both succeed (envelope code 0), non-zero with diagnostics on failure.
 */
auto RunAuthSmokeTest() -> int {
    using namespace disk::qml;

    // Construct minimal dependencies
    utils::ConfigStore configStore;
    services::TokenStore tokenStore;

    // Check for valid token
    if (!tokenStore.HasValidAccessToken()) {
        qCritical() << "Auth smoke test failed: No valid access token found";
        qCritical() << "Token file:" << (tokenStore.AccessToken().isEmpty() ? "(empty or missing)" : "(expired)");
        return 1;
    }

    QString accessToken = tokenStore.AccessToken();
    if (accessToken.isEmpty()) {
        qCritical() << "Auth smoke test failed: Access token is empty";
        return 1;
    }

    api::ApiClient apiClient;
    apiClient.SetBaseUrl(configStore.ServerUrl());
    apiClient.SetBearerToken(accessToken);

    api::FileApi fileApi(&apiClient);
    api::FolderApi folderApi(&apiClient);

    // Event loop for async operations
    QEventLoop loop;

    int exitCode = 0;
    int pendingCalls = 2; // Two API calls to complete
    bool hadError = false;

    // 10-second timeout
    QTimer::singleShot(10000, &loop, [&loop, &hadError]() {
        if (!hadError) {
            qCritical() << "Auth smoke test failed: Timeout (10s)";
            hadError = true;
        }
        loop.quit();
    });

    // Lambda to check completion and exit
    auto checkCompletion = [&]() {
        pendingCalls--;
        if (pendingCalls == 0 || hadError) {
            loop.quit();
        }
    };

    // Call 1: GET /api/file/list
    fileApi.List(
        0,                  // parent_id = root
        1,                  // page
        1,                  // page_size (minimal)
        QStringLiteral("name"),
        QStringLiteral("asc"),
        QStringLiteral("all"),
        &loop,
        [&hadError, &exitCode, &checkCompletion](models::ApiEnvelope envelope, QString networkError) {
            if (!networkError.isEmpty()) {
                qCritical() << "Auth smoke test failed: File list network error:" << networkError;
                hadError = true;
                exitCode = 2;
            } else if (envelope.IsError()) {
                qCritical() << "Auth smoke test failed: File list API error (code=" << envelope.code << "):" << envelope.message;
                hadError = true;
                exitCode = 3;
            } else {
                qDebug() << "Auth smoke test: File list OK";
            }
            checkCompletion();
        }
    );

    // Call 2: GET /api/folder/0/breadcrumb
    folderApi.GetBreadcrumb(
        0,  // folder_id = root
        &loop,
        [&hadError, &exitCode, &checkCompletion](models::ApiEnvelope envelope, QString networkError) {
            if (!networkError.isEmpty()) {
                qCritical() << "Auth smoke test failed: Breadcrumb network error:" << networkError;
                hadError = true;
                exitCode = 4;
            } else if (envelope.IsError()) {
                qCritical() << "Auth smoke test failed: Breadcrumb API error (code=" << envelope.code << "):" << envelope.message;
                hadError = true;
                exitCode = 5;
            } else {
                qDebug() << "Auth smoke test: Breadcrumb OK";
            }
            checkCompletion();
        }
    );

    loop.exec();

    if (!hadError && exitCode == 0) {
        qDebug() << "Auth smoke test passed: All API calls succeeded";
    }

    return hadError ? exitCode : 0;
}

int main(int argc, char* argv[]) {
    // --- Early CLI parsing for --auth-smoke (before QGuiApplication) ---
    bool isAuthSmoke = false;
    for (int i = 1; i < argc; ++i) {
        if (QString::fromLocal8Bit(argv[i]) == QLatin1String("--auth-smoke")) {
            isAuthSmoke = true;
            break;
        }
    }

    // --- Auth smoke mode: headless, no QML ---
    if (isAuthSmoke) {
        QCoreApplication app(argc, argv);
        QCoreApplication::setOrganizationName("Disk");
        QCoreApplication::setApplicationName("diskqml");

        // Run the smoke test and exit
        return RunAuthSmokeTest();
    }

    // --- Normal QML UI mode ---
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
    disk::qml::services::FileService fileService(&fileApi);
    disk::qml::services::FolderService folderService(&folderApi);
    disk::qml::services::TrashService trashService(&trashApi);

    disk::qml::viewmodels::LoginViewModel loginViewModel(&authService);
    disk::qml::viewmodels::RegisterViewModel registerViewModel(&authService);

    disk::qml::viewmodels::SessionViewModel sessionViewModel(
        &loginViewModel,
        &tokenStore,
        &authService,
        &configStore
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

    // --- CLI parsing ---
    QCommandLineParser parser;
    parser.setApplicationDescription("Disk QML desktop client");
    parser.addHelpOption();

    QCommandLineOption smokeOption(
        "smoke",
        "Headless smoke mode: quit after QML loads (no network required)");
    parser.addOption(smokeOption);

    QCommandLineOption authSmokeOption(
        "auth-smoke",
        "Headless auth verification: test API calls with stored token (exits 0 on success)");
    parser.addOption(authSmokeOption);

    parser.process(app);

    if (parser.isSet(smokeOption)) {
        QTimer::singleShot(1500, &app, &QCoreApplication::quit);
    }

    // Note: --auth-smoke is handled before QGuiApplication, so if we're here,
    // the flag was not set or was already processed.

    return app.exec();
}
