
#include <memory>
#include <iostream>

#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>

#include "AppEngine.h"
#include "logging.h"

using namespace std;

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);

    // Set application information
    QCoreApplication::setOrganizationName("The Last Viking LTD");
    QCoreApplication::setApplicationName("QVocalWriter");

    // Set up a log-handler to stdout
    // TODO: Make this configurable via command-line arguments or config file
    logfault::LogManager::Instance().AddHandler(make_unique<logfault::StreamHandler>(clog, logfault::LogLevel::TRACE));
    // TODO: Enable logging to file

    AppEngine app_engine;

    QQmlApplicationEngine qml_engine;

    qml_engine.rootContext()->setContextProperty("appEngine", &app_engine);

    //LOG_TRACE_N << "Registering static models for QML...";
    //qmlRegisterSingletonInstance<AppEngine>("AppEngine", 1, 0, "AppEngine", &app_engine);


    LFLOG_INFO << "Logging to std::clog is enabled";

    QObject::connect(
        &qml_engine,
        &QQmlApplicationEngine::objectCreationFailed,
        &app,
        []() { QCoreApplication::exit(-1); },
        Qt::QueuedConnection);

    qml_engine.load(QUrl(QStringLiteral("qrc:/QVocalWriter/qml/Main.qml")));

    return app.exec();
}
