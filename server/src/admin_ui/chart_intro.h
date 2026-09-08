#pragma once

#include <QEvent>
#include <QVariantAnimation>
#include <QWidget>

namespace charging::server {

// One finite reveal per explicit UI trigger. Data updates and hidden pages
// always settle immediately, so browsing history never replays a transition.
class ChartIntro final : public QVariantAnimation {
public:
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
    qreal progress() const { return state() == Running ? currentValue().toReal() : 1.0; }
    void replay() { stop(); start(); }
    void finish() { stop(); setCurrentTime(duration()); }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (event->type() == QEvent::Hide) finish();
        return QVariantAnimation::eventFilter(watched, event);
    }
};

} // namespace charging::server
