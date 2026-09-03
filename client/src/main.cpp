#include "api/mock_charging_api.h"
#include "ui/main_window.h"

#include <QApplication>

int main(int argc, char *argv[])
{
    QApplication application(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("BIT"));
    QApplication::setApplicationName(QStringLiteral("ChargingClient"));

    charging::client::MockChargingApi api;
    charging::client::MainWindow window(api);
    window.show();

    return application.exec();
}
