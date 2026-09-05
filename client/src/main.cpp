#include "api/mock_charging_api.h"
#include "api/tcp_charging_api.h"
#include "assistant/assistant_config.h"
#include "local/input_method_setup.h"
#include "local/i_map_service.h"
#include "local/mock_map_service.h"
#include "local/tencent_map_service.h"
#include "ui/main_window.h"

#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QDebug>

#include <memory>

int main(int argc, char *argv[])
{
    (void)charging::client::configureInputMethodForQt();
    QApplication application(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("BIT"));
    QApplication::setApplicationName(QStringLiteral("ChargingClient"));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("BIT_2026_CS Qt 用户端"));
    parser.addHelpOption();
    const QCommandLineOption apiOption(
        QStringLiteral("api"),
        QStringLiteral("API adapter: mock or tcp (default: mock)."),
        QStringLiteral("adapter"),
        QStringLiteral("mock"));
    const QCommandLineOption hostOption(
        QStringLiteral("host"),
        QStringLiteral("TCP server host (default: 127.0.0.1)."),
        QStringLiteral("host"),
        QStringLiteral("127.0.0.1"));
    const QCommandLineOption portOption(
        QStringLiteral("port"),
        QStringLiteral("TCP server port (default: 45678)."),
        QStringLiteral("port"),
        QStringLiteral("45678"));
    const QCommandLineOption timeoutOption(
        QStringLiteral("timeout-ms"),
        QStringLiteral("Network request timeout in milliseconds (default: 5000)."),
        QStringLiteral("milliseconds"),
        QStringLiteral("5000"));
    const QCommandLineOption mapOption(
        QStringLiteral("map"),
        QStringLiteral("Map adapter: mock or tencent (default: mock)."),
        QStringLiteral("adapter"),
        QStringLiteral("mock"));
    const QCommandLineOption assistantConfigOption(
        QStringLiteral("assistant-config"),
        QStringLiteral("Path to local AI configuration (never pass a Key on the command line)."),
        QStringLiteral("path"));
    parser.addOptions({apiOption, hostOption, portOption, timeoutOption, mapOption,
                       assistantConfigOption});
    parser.process(application);

    bool timeoutOk = false;
    const int timeoutMs = parser.value(timeoutOption).toInt(&timeoutOk);
    if (!timeoutOk || timeoutMs < 1) {
        qCritical().noquote()
            << QStringLiteral("Invalid network request timeout.");
        return 2;
    }

    std::unique_ptr<charging::client::IChargingApi> api;
    const QString adapter = parser.value(apiOption).trimmed().toLower();
    if (adapter == QStringLiteral("mock")) {
        api = std::make_unique<charging::client::MockChargingApi>();
    } else if (adapter == QStringLiteral("tcp")) {
        bool portOk = false;
        const int port = parser.value(portOption).toInt(&portOk);
        const QString host = parser.value(hostOption).trimmed();
        if (!portOk || port < 1 || port > 65535 || host.isEmpty()) {
            qCritical().noquote()
                << QStringLiteral("Invalid TCP host, port, or timeout.");
            return 2;
        }
        api = std::make_unique<charging::client::TcpChargingApi>(
            host, static_cast<quint16>(port), timeoutMs);
        qInfo().noquote()
            << QStringLiteral("API mode: TCP %1:%2").arg(host).arg(port);
    } else {
        qCritical().noquote()
            << QStringLiteral("Unknown API adapter: %1 (expected mock or tcp)")
                   .arg(adapter);
        return 2;
    }

    std::unique_ptr<charging::client::IMapService> mapService;
    const QString mapAdapter = parser.value(mapOption).trimmed().toLower();
    if (mapAdapter == QStringLiteral("mock")) {
        mapService = std::make_unique<charging::client::MockMapService>();
    } else if (mapAdapter == QStringLiteral("tencent")) {
#ifndef CHARGING_CLIENT_HAS_WEBENGINE
        qCritical().noquote()
            << QStringLiteral("Tencent map support is not built. Reconfigure with "
                              "-DCHARGING_CLIENT_ENABLE_WEBENGINE=ON.");
        return 2;
#else
        const QString apiKey =
            qEnvironmentVariable("TENCENT_MAP_KEY").trimmed();
        if (apiKey.isEmpty()) {
            qCritical().noquote()
                << QStringLiteral("TENCENT_MAP_KEY is required for --map tencent.");
            return 2;
        }
        mapService = std::make_unique<charging::client::TencentMapService>(
            apiKey, timeoutMs);
        qInfo().noquote() << QStringLiteral("Map mode: Tencent");
#endif
    } else {
        qCritical().noquote()
            << QStringLiteral("Unknown map adapter: %1 (expected mock or tencent)")
                   .arg(mapAdapter);
        return 2;
    }

    const auto assistantConfig = charging::client::AssistantConfig::load(
        parser.value(assistantConfigOption));
    charging::client::MainWindow window(*api, *mapService, assistantConfig);
    window.show();

    return application.exec();
}
