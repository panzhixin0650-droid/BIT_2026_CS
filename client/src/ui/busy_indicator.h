#pragma once

#include <QElapsedTimer>
#include <QTimer>
#include <QWidget>

namespace charging::client {

class BusyIndicator final : public QWidget {
public:
    explicit BusyIndicator(QWidget *parent = nullptr);
    void setRunning(bool running);
    int elapsedSeconds() const { return elapsed_.isValid() ? int(elapsed_.elapsed() / 1000) : 0; }
protected:
    void paintEvent(QPaintEvent *) override;
private:
    QTimer animation_;
    QElapsedTimer elapsed_;
    int angle_ = 0;
};

}  // namespace charging::client
