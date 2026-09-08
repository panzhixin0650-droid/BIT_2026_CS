#pragma once

#include <QWidget>

namespace charging::client {

// A reserved artwork column: never overlays the heading or its controls.
class DecorativeHeading final : public QWidget {
public:
    DecorativeHeading(QWidget *text, const QString &asset, int extent,
                      qreal rotation = 0, QWidget *parent = nullptr);
protected:
    void resizeEvent(QResizeEvent *event) override;
private:
    QWidget *art_;
    int extent_;
};

} // namespace charging::client
