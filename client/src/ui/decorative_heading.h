// 文件用途：装饰标题控件的声明
#pragma once

#include <QWidget>

namespace charging::client {

// A reserved artwork column: never overlays the heading or its controls.
class DecorativeHeading final : public QWidget {
public:
    // 传入文字控件、图片名与尺寸即可组合出标题行
    DecorativeHeading(QWidget *text, const QString &asset, int extent,
                      qreal rotation = 0, QWidget *parent = nullptr);
protected:
    void resizeEvent(QResizeEvent *event) override;
private:
    QWidget *art_;
    int extent_;
};

} // namespace charging::client
