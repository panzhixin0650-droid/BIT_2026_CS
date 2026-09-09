// 仪表盘指标标签：数字带入场动画，文本与布局保持不变
#pragma once

#include "chart_intro.h"
#include <QLabel>
#include <QPainter>
#include <QRegularExpression>
#include <QStyle>
#include <QStyleOption>

namespace charging::server {

// Animate only painted digits: the label retains its authoritative text and
// final size throughout, keeping layout and accessibility stable.
class AnimatedMetricLabel final : public QLabel {
public:
    explicit AnimatedMetricLabel(const QString &text, QWidget *parent)
        : QLabel(text, parent), intro_(new ChartIntro(this))
    {
        intro_->setObjectName(QStringLiteral("metricIntroAnimation"));
    }

    // 文本含数字才播放动画，否则直接显示最终值
    void playIntro()
    {
        if (text().contains(QRegularExpression(QStringLiteral("[0-9]")))) intro_->replay();
        else finishIntro();
    }
    void finishIntro() { intro_->finish(); }

    // 按动画进度缩放文本中的数字，保留原有小数位
    QString displayedText() const
    {
        if (intro_->progress() >= 1.0) return text();
        static const QRegularExpression digits(QStringLiteral("[0-9]+(?:\\.[0-9]+)?"));
        const QString target = text();
        auto matches = digits.globalMatch(target);
        QString result;
        qsizetype offset = 0;
        while (matches.hasNext()) {
            const auto match = matches.next();
            result += target.mid(offset, match.capturedStart() - offset);
            const auto number = match.captured();
            const auto dot = number.indexOf('.');
            const int precision = dot < 0 ? 0 : number.size() - dot - 1;
            result += QString::number(number.toDouble() * intro_->progress(), 'f', precision);
            offset = match.capturedEnd();
        }
        return result + target.mid(offset);
    }

protected:
    // 动画进行中自行绘制过渡文本，结束后交回 QLabel 绘制
    void paintEvent(QPaintEvent *event) override
    {
        if (intro_->state() != QAbstractAnimation::Running) {
            QLabel::paintEvent(event);
            return;
        }
        QPainter painter(this);
        QStyleOption option;
        option.initFrom(this);
        style()->drawPrimitive(QStyle::PE_Widget, &option, &painter, this);
        painter.setFont(font());
        style()->drawItemText(&painter, contentsRect(), alignment(), palette(),
                              isEnabled(), displayedText(), foregroundRole());
    }

private:
    ChartIntro *intro_;
};

} // namespace charging::server
