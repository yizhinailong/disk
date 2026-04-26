#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>

#include "app/Application.hpp"

int main(int argc, char* argv[]) {
    QGuiApplication app(argc, argv);
    app.setOrganizationName("Disk");
    app.setApplicationName("Disk Desktop");

    disk::app::Application diskApp;

    QQmlApplicationEngine engine;

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
