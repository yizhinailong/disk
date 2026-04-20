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

    const QUrl url(u"qrc:/qt/qml/DiskDesktop/qml/Main.qml"_qs);

    QObject::connect(
        &engine,
        &QQmlApplicationEngine::objectCreated,
        &app,
        [url](QObject* obj, const QUrl& objUrl) {
            if (!obj && url == objUrl) {
                QCoreApplication::exit(-1);
            }
        },
        Qt::QueuedConnection
    );

    engine.load(url);
    return app.exec();
}
