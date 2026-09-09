// 旋转忙碌指示控件，用于等待请求返回时的提示
#pragma once

#include <QElapsedTimer>
#include <QTimer>
#include <QWidget>

namespace charging::client {

class BusyIndicator final : public QWidget {
public:
    explicit BusyIndicator(QWidget *parent = nullptr);
    // 启动或停止动画，同时控制计时
    void setRunning(bool running);
    // 返回已等待秒数，未启动时为 0
    int elapsedSeconds() const { return elapsed_.isValid() ? int(elapsed_.elapsed() / 1000) : 0; }
protected:
    void paintEvent(QPaintEvent *) override;
private:
    QTimer animation_;
    QElapsedTimer elapsed_;
    int angle_ = 0;
};

}  // namespace charging::client
