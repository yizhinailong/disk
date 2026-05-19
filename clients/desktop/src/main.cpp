#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>

#include "app/Application.hpp"
#include "utils/ClipboardBridge.hpp"

int main(int argc, char* argv[]) {
    QGuiApplication app(argc, argv);
    app.setOrganizationName("Disk");
    app.setApplicationName("Disk Desktop");

    // The QML controls customize background/contentItem, which native styles reject.
    QQuickStyle::setStyle(QStringLiteral("Basic"));

    disk::app::Application diskApp;
    disk::utils::ClipboardBridge clipboard;

    QQmlApplicationEngine engine;

    engine.rootContext()->setContextProperty(QStringLiteral("Clipboard"), &clipboard);

    diskApp.Initialize(&engine);

    QObject::connect(
        &engine,
        &QQmlApplicationEngine::objectCreationFailed,
        &app,
        []() { QCoreApplication::exit(-1); },
        Qt::QueuedConnection
    );

    engine.loadFromModule(QStringLiteral("DiskDesktop"), QStringLiteral("Main"));
    return app.exec();
}
