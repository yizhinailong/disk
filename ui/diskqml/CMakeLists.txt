cmake_minimum_required(VERSION 3.16)

project(diskqml VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Quick QuickControls2 Network)

qt_standard_project_setup(REQUIRES 6.8)

# --- diskqml_core static library ---
# Pattern: All non-QML-facing C++ sources go here (api, services, models, utils, transfers)
# DO NOT add QObject/QAbstractListModel classes with QML_ELEMENT to this target.
add_library(diskqml_core STATIC
    src/api/ApiClient.cpp
    src/api/AuthApi.cpp
    # --- Future: api services (FileApi, FolderApi, ShareApi, TrashApi, UserApi) ---
    # src/api/FileApi.cpp
    # src/api/FolderApi.cpp
    # src/api/ShareApi.cpp
    # src/api/TrashApi.cpp
    src/api/UserApi.cpp
    src/services/AuthService.cpp
    src/services/TokenStore.cpp
    src/services/TokenRefreshCoordinator.cpp
    # --- Future: services (FileService, FolderService, ShareService, TrashService, UserService) ---
    # src/services/FileService.cpp
    # src/services/FolderService.cpp
    # src/services/ShareService.cpp
    # src/services/TrashService.cpp
    # src/services/UserService.cpp
    src/utils/ConfigStore.cpp
    # --- Future: models (FileListModel, BreadcrumbModel, TransferQueueModel) ---
    # src/models/FileListModel.cpp
    # src/models/BreadcrumbModel.cpp
    # src/models/TransferQueueModel.cpp
    # --- Future: transfers subsystem ---
    # src/transfers/TransferManager.cpp
    # src/transfers/UploadTask.cpp
    # src/transfers/DownloadTask.cpp
)

target_include_directories(diskqml_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)

target_link_libraries(diskqml_core
    PUBLIC Qt6::Core Qt6::Network
)

# --- Main executable ---
qt_add_executable(appdiskqml
    src/main.cpp
)

# Pattern: QML-facing viewmodels (QObject/QAbstractListModel with QML_ELEMENT) go in SOURCES.
# All QML pages, components, and dialogs go in QML_FILES.
qt_add_qml_module(appdiskqml
    URI Disk
    VERSION 1.0
    SOURCES
        src/viewmodels/LoginViewModel.cpp
        src/viewmodels/LoginViewModel.hpp
        src/viewmodels/RegisterViewModel.cpp
        src/viewmodels/RegisterViewModel.hpp
        src/viewmodels/SessionViewModel.cpp
        src/viewmodels/SessionViewModel.hpp
        # --- Future: viewmodels (FileListViewModel, TransferViewModel, ShareViewModel, TrashViewModel, SettingsViewModel) ---
        # src/viewmodels/FileListViewModel.cpp
        # src/viewmodels/FileListViewModel.hpp
        # src/viewmodels/TransferViewModel.cpp
        # src/viewmodels/TransferViewModel.hpp
        # src/viewmodels/ShareViewModel.cpp
        # src/viewmodels/ShareViewModel.hpp
        # src/viewmodels/TrashViewModel.cpp
        # src/viewmodels/TrashViewModel.hpp
        # src/viewmodels/SettingsViewModel.cpp
        # src/viewmodels/SettingsViewModel.hpp
    QML_FILES
        qml/Main.qml
        qml/App.qml
        qml/views/LoginView.qml
        qml/views/RegisterView.qml
        qml/views/HomePlaceholder.qml
        qml/views/MainWindowView.qml
        # --- Future: QML pages/components/dialogs ---
        # qml/views/FileListView.qml
        # qml/views/TrashView.qml
        # qml/views/ShareView.qml
        # qml/views/SettingsView.qml
        # qml/components/FileCard.qml
        # qml/components/BreadcrumbBar.qml
        # qml/components/TransferPanel.qml
        # qml/dialogs/RenameDialog.qml
        # qml/dialogs/ShareDialog.qml
        # qml/dialogs/ConfirmDialog.qml
)

target_include_directories(appdiskqml PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/viewmodels)
# Qt for iOS sets MACOSX_BUNDLE_GUI_IDENTIFIER automatically since Qt 6.1.
# If you are developing for iOS or macOS you should consider setting an
# explicit, fixed bundle identifier manually though.
set_target_properties(appdiskqml PROPERTIES
#    MACOSX_BUNDLE_GUI_IDENTIFIER com.example.appdiskqml
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
)

target_link_libraries(appdiskqml
    PRIVATE diskqml_core Qt6::Quick Qt6::QuickControls2
)

include(GNUInstallDirs)
install(TARGETS appdiskqml
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

