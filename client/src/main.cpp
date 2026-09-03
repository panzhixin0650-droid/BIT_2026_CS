#include "ui/main_window.h"

#include <QApplication>

int main(int argc, char *argv[])
{
    QApplication application(argc, argv);

    charging::client::MainWindow window;
    window.show();

    return application.exec();
}
