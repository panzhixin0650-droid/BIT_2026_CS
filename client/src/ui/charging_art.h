#pragma once

#include <QWidget>

namespace charging::client {

// Decorative, resolution-independent artwork; never displays simulated telemetry.
class ChargingArt final : public QWidget {
public:
    enum class Scene { Welcome, Journey, Scan };
    explicit ChargingArt(Scene scene, QWidget *parent = nullptr);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    Scene scene_;
};

}  // namespace charging::client
