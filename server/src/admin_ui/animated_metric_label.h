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

    void playIntro()
    {
        if (text().contains(QRegularExpression(QStringLiteral("[0-9]")))) intro_->replay();
        else finishIntro();
    }
    void finishIntro() { intro_->finish(); }

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
