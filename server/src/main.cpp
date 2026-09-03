#include "admin_ui/admin_facade.h"
#include "admin_ui/admin_window.h"
#include "application/application_service.h"
#include "application/session_store.h"
#include "adapters/mock_pile.h"
#include "adapters/mock_prediction_provider.h"
#include "persistence/in_memory_repository.h"
#include "transport/request_router.h"
#include "transport/tcp_gateway.h"

#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QDebug>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("server-app"));
    QCoreApplication::setApplicationVersion(QStringLiteral("0.1.0"));

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("BIT_2026_CS Qt 服务端与管理员端 Demo"));
    parser.addHelpOption();
    parser.addVersionOption();

    const QCommandLineOption portOption(
        {QStringLiteral("p"), QStringLiteral("port")},
        QStringLiteral("TCP listen port (default: 45678)."),
        QStringLiteral("port"),
        QStringLiteral("45678"));
    const QCommandLineOption noTcpOption(
        QStringLiteral("no-tcp"),
        QStringLiteral("Start the administrator window without TCP."));
    parser.addOption(portOption);
    parser.addOption(noTcpOption);
    parser.process(app);

    bool portOk = false;
    const int requestedPort = parser.value(portOption).toInt(&portOk);
    const quint16 port = (portOk && requestedPort >= 0 && requestedPort <= 65535)
        ? static_cast<quint16>(requestedPort)
        : quint16{45678};

    charging::server::InMemoryRepository repository;
    charging::server::SessionStore sessions;
    charging::server::MockPile pileGateway;
    charging::server::MockPredictionProvider predictions;
    charging::server::ApplicationService service(
        &repository, &sessions, &pileGateway, &predictions);
    charging::server::RequestRouter router(&service);
    charging::server::TcpGateway gateway(&router);

    bool tcpStarted = false;
    if (!parser.isSet(noTcpOption)) {
        QString error;
        tcpStarted = gateway.start(port, QHostAddress::LocalHost, &error);
        if (!tcpStarted) {
            qWarning().noquote() << QStringLiteral("TCP Gateway failed to start: %1")
                                        .arg(error);
        }
    }

    charging::server::AdminFacade facade(&service);
    charging::server::AdminWindow window(&facade, tcpStarted, gateway.serverPort());
    window.show();
    return app.exec();
}
