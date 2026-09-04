#include "api/mock_charging_api.h"
#include "api/tcp_charging_api.h"
#include "local/input_method_setup.h"
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
        QStringLiteral("TCP request timeout in milliseconds (default: 5000)."),
        QStringLiteral("milliseconds"),
        QStringLiteral("5000"));
    parser.addOptions({apiOption, hostOption, portOption, timeoutOption});
    parser.process(application);

    std::unique_ptr<charging::client::IChargingApi> api;
    const QString adapter = parser.value(apiOption).trimmed().toLower();
    if (adapter == QStringLiteral("mock")) {
        api = std::make_unique<charging::client::MockChargingApi>();
    } else if (adapter == QStringLiteral("tcp")) {
        bool portOk = false;
        bool timeoutOk = false;
        const int port = parser.value(portOption).toInt(&portOk);
        const int timeoutMs = parser.value(timeoutOption).toInt(&timeoutOk);
        const QString host = parser.value(hostOption).trimmed();
        if (!portOk || port < 1 || port > 65535 || !timeoutOk
            || timeoutMs < 1 || host.isEmpty()) {
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

    charging::client::MainWindow window(*api);
    window.show();

    return application.exec();
}
