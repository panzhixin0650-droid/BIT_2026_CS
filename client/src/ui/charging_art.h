// 充电场景插画控件声明
#pragma once

#include <QWidget>

namespace charging::client {

// Decorative, resolution-independent artwork; never displays simulated telemetry.
class ChargingArt final : public QWidget {
public:
    // 三种场景：欢迎页、旅程页与扫码页
    enum class Scene { Welcome, Journey, Scan };
    explicit ChargingArt(Scene scene, QWidget *parent = nullptr);

protected:
    // 所有图形都在绘制事件中即时画出
    void paintEvent(QPaintEvent *event) override;

private:
    Scene scene_;
};

}  // namespace charging::client
