
#include <memory>
#include <iostream>

#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QSettings>

#include "AppEngine.h"
#include "logging.h"

using namespace std;

namespace {
optional<logfault::LogLevel> toLogLevel(string_view name) {
    if (name.empty() || name == "off" || name == "false") {
        return {};
    }

    if (name == "debug") {
        return logfault::LogLevel::DEBUGGING;
    }

    if (name == "trace") {
        return logfault::LogLevel::TRACE;
    }

    return logfault::LogLevel::INFO;
}
} // namespace

int main(int argc, char *argv[])
{   
    QGuiApplication app(argc, argv);

    // Set application information
    QCoreApplication::setOrganizationName("The Last Viking LTD");
    QCoreApplication::setApplicationName("QVocalWriter");

    QSettings settings;

    // Set up a log-handler to stdout
    // TODO: Make this configurable via command-line arguments or config file
    // logfault::LogManager::Instance().AddHandler(make_unique<logfault::StreamHandler>(clog, logfault::LogLevel::DEBUGGING));
    // LFLOG_INFO << "Logging to std::clog is enabled";

    AppEngine::initLogging();

    LOG_INFO << "Starting QVocalWriter " << APP_VERSION;
    LOG_INFO << "Configuration from '" << settings.fileName() << "'";

    AppEngine app_engine;

    QQmlApplicationEngine qml_engine;
    qml_engine.rootContext()->setContextProperty("appEngine", &app_engine);

    QObject::connect(
        &qml_engine,
        &QQmlApplicationEngine::objectCreationFailed,
        &app,
        []() { QCoreApplication::exit(-1); },
        Qt::QueuedConnection);

    qml_engine.load(QUrl(QStringLiteral("qrc:/QVocalWriter/qml/Main.qml")));

    return app.exec();
}
