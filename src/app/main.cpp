
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
    logfault::LogManager::Instance().AddHandler(make_unique<logfault::StreamHandler>(clog, logfault::LogLevel::DEBUGGING));
    LFLOG_INFO << "Logging to std::clog is enabled";

    if (const auto& log_file = settings.value("logging/log_file", "/tmp/qvocalwriter.log").toString(); !log_file.isEmpty()) {
        const auto log_level = settings.value("logging/log_level", "trace").toString();
        if (const auto level = toLogLevel(log_level.toStdString()); level.has_value()) {
                logfault::LogManager::Instance().AddHandler(make_unique<logfault::StreamHandler>(log_file.toStdString(),*level, true));
                LOG_INFO_N << "Logging to file '" << log_file;
        }
    }

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
