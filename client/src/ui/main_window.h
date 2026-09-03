#pragma once

#include <QMainWindow>

namespace charging::client {

class MainWindow final : public QMainWindow {
public:
    explicit MainWindow(QWidget *parent = nullptr);
};

}  // namespace charging::client
