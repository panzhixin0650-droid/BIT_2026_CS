// 图表入场动画的小工具类，供各图表控件共用进度值
#pragma once

#include <QEvent>
#include <QVariantAnimation>
#include <QWidget>

namespace charging::server {

// One finite reveal per explicit UI trigger. Data updates and hidden pages
// always settle immediately, so browsing history never replays a transition.
class ChartIntro final : public QVariantAnimation {
public:
    // 构造时绑定宿主控件：值变化触发重绘，隐藏时自动收尾
    explicit ChartIntro(QWidget *owner) : QVariantAnimation(owner)
    {
        setObjectName(QStringLiteral("chartIntroAnimation"));
        setDuration(900);
        setStartValue(0.0);
        setEndValue(1.0);
        setEasingCurve(QEasingCurve::OutCubic);
        connect(this, &QVariantAnimation::valueChanged, owner, [owner] { owner->update(); });
        owner->installEventFilter(this);
    }
    // 未运行时进度直接返回1，绘制代码可无条件使用
    qreal progress() const { return state() == Running ? currentValue().toReal() : 1.0; }
    void replay() { stop(); start(); }
    void finish() { stop(); setCurrentTime(duration()); }

protected:
    // 控件隐藏事件到达时立即结束动画，避免再次显示时残留
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (event->type() == QEvent::Hide) finish();
        return QVariantAnimation::eventFilter(watched, event);
    }
};

} // namespace charging::server
