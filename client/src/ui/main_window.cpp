#include "ui/main_window.h"

#include <QLabel>

namespace charging::client {

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setObjectName(QStringLiteral("mainWindow"));
    setWindowTitle(QStringLiteral("新能源汽车充电服务"));
    resize(420, 760);

    auto *placeholder = new QLabel(QStringLiteral("用户端正在建设中"), this);
    placeholder->setObjectName(QStringLiteral("appPlaceholder"));
    placeholder->setAlignment(Qt::AlignCenter);
    setCentralWidget(placeholder);
}

}  // namespace charging::client
