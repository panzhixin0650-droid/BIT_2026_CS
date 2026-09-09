// 管理端下拉框：在扁平样式上手绘箭头指示符
#pragma once

#include <QComboBox>
#include <QPainter>
#include <QPainterPath>
#include <QStyleOptionComboBox>

namespace charging::server {

// Preserve the native popup/keyboard behavior while drawing a crisp indicator
// on the flat, stylesheet-painted control without an image-format dependency.
// 继承 QComboBox 保留原生弹出与键盘行为
class AdminComboBox final : public QComboBox {
public:
    using QComboBox::QComboBox;

protected:
    // 先画默认外观，再补画箭头
    void paintEvent(QPaintEvent *event) override
    {
        QComboBox::paintEvent(event);
        QStyleOptionComboBox option;
        initStyleOption(&option);
        // 从样式取箭头区域，保证不同缩放下位置正确
        const auto arrow = style()->subControlRect(QStyle::CC_ComboBox, &option,
                                                   QStyle::SC_ComboBoxArrow, this);
        const QPointF center = QRectF(arrow).center();
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(QPen(QColor(isEnabled() ? "#52637c" : "#a1adbf"),
                            1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        QPainterPath path;
        path.moveTo(center + QPointF(-4, -2));
        path.lineTo(center + QPointF(0, 2));
        path.lineTo(center + QPointF(4, -2));
        painter.drawPath(path);
    }
};

} // namespace charging::server
