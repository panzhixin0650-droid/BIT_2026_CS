#include "api/mock_charging_api.h"
#include "local/input_method_setup.h"
#include "ui/main_window.h"

#include <QApplication>

int main(int argc, char *argv[])
{
    (void)charging::client::configureInputMethodForQt();
    QApplication application(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("BIT"));
    QApplication::setApplicationName(QStringLiteral("ChargingClient"));

    charging::client::MockChargingApi api;
    charging::client::MainWindow window(api);
    window.show();

    return application.exec();
}
