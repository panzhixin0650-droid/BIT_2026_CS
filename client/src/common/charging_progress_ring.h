#pragma once

#include <QWidget>

namespace charging::client {

class ChargingProgressRing final : public QWidget {
public:
    explicit ChargingProgressRing(QWidget *parent = nullptr,
                                  const QString &objectName = QStringLiteral("chargingProgressRing"));

    void setProgress(int percent, const QString &caption);
    [[nodiscard]] int progress() const { return percent_; }

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    int percent_ = 0;
    QString caption_ = QStringLiteral("等待开始");
};

}  // namespace charging::client
