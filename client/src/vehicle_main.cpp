#include "api/mock_charging_api.h"
#include "api/tcp_charging_api.h"
#include "assistant/assistant_config.h"
#include "local/input_method_setup.h"
#include "local/mock_map_service.h"
#include "local/tencent_map_service.h"
#include "vehicle/vehicle_main_window.h"

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
    QApplication::setApplicationName(QStringLiteral("VehicleChargingClient"));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("BIT_2026_CS 悦充车载端"));
    parser.addHelpOption();
    const QCommandLineOption apiOption(QStringLiteral("api"), QStringLiteral("API adapter: mock or tcp."),
                                       QStringLiteral("adapter"), QStringLiteral("mock"));
    const QCommandLineOption hostOption(QStringLiteral("host"), QStringLiteral("TCP server host."),
                                        QStringLiteral("host"), QStringLiteral("127.0.0.1"));
    const QCommandLineOption portOption(QStringLiteral("port"), QStringLiteral("TCP server port."),
                                        QStringLiteral("port"), QStringLiteral("45678"));
    const QCommandLineOption timeoutOption(QStringLiteral("timeout-ms"), QStringLiteral("Request timeout."),
                                           QStringLiteral("milliseconds"), QStringLiteral("5000"));
    const QCommandLineOption mapOption(QStringLiteral("map"), QStringLiteral("Map adapter: mock or tencent."),
                                       QStringLiteral("adapter"), QStringLiteral("mock"));
    const QCommandLineOption assistantOption(QStringLiteral("assistant-config"),
                                              QStringLiteral("Path to local AI configuration."),
                                              QStringLiteral("path"));
    parser.addOptions({apiOption, hostOption, portOption, timeoutOption, mapOption, assistantOption});
    parser.process(application);

    bool timeoutOk = false;
    const int timeoutMs = parser.value(timeoutOption).toInt(&timeoutOk);
    if (!timeoutOk || timeoutMs < 1) {
        qCritical().noquote() << QStringLiteral("Invalid network request timeout.");
        return 2;
    }

    std::unique_ptr<charging::client::IChargingApi> api;
    const QString apiMode = parser.value(apiOption).trimmed().toLower();
    if (apiMode == QStringLiteral("mock")) {
        api = std::make_unique<charging::client::MockChargingApi>();
    } else if (apiMode == QStringLiteral("tcp")) {
        bool portOk = false;
        const int port = parser.value(portOption).toInt(&portOk);
        const QString host = parser.value(hostOption).trimmed();
        if (!portOk || port < 1 || port > 65535 || host.isEmpty()) {
            qCritical().noquote() << QStringLiteral("Invalid TCP host or port.");
            return 2;
        }
        api = std::make_unique<charging::client::TcpChargingApi>(host, quint16(port), timeoutMs);
    } else {
        qCritical().noquote() << QStringLiteral("Unknown API adapter: %1").arg(apiMode);
        return 2;
    }

    std::unique_ptr<charging::client::IMapService> map;
    const QString mapMode = parser.value(mapOption).trimmed().toLower();
    if (mapMode == QStringLiteral("mock")) {
        map = std::make_unique<charging::client::MockMapService>();
    } else if (mapMode == QStringLiteral("tencent")) {
#ifndef CHARGING_CLIENT_HAS_WEBENGINE
        qCritical().noquote() << QStringLiteral(
            "Tencent map support is not built. Reconfigure with -DCHARGING_CLIENT_ENABLE_WEBENGINE=ON.");
        return 2;
#else
        const QString key = qEnvironmentVariable("TENCENT_MAP_KEY").trimmed();
        if (key.isEmpty()) {
            qCritical().noquote() << QStringLiteral("TENCENT_MAP_KEY is required for --map tencent.");
            return 2;
        }
        auto tencent = std::make_unique<charging::client::TencentMapService>(key, timeoutMs);
        if (tencent->mapScriptUrl().isEmpty()) {
            qCritical().noquote() << QStringLiteral("TENCENT_MAP_KEY 格式无效。");
            return 2;
        }
        map = std::move(tencent);
#endif
    } else {
        qCritical().noquote() << QStringLiteral("Unknown map adapter: %1").arg(mapMode);
        return 2;
    }

    const auto assistantConfig = charging::client::AssistantConfig::load(parser.value(assistantOption));
    charging::client::VehicleMainWindow window(*api, *map, assistantConfig);
    window.show();
    return application.exec();
}
