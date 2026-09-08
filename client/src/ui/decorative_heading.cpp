#include "ui/decorative_heading.h"

#include <QHBoxLayout>
#include <QPainter>
#include <QPixmap>
#include <QResizeEvent>

static void initializeDecorations()
{
    Q_INIT_RESOURCE(decorations);
}

namespace charging::client {
namespace {
class Accent final : public QWidget {
public:
    Accent(const QString &asset, qreal rotation, QWidget *parent)
        : QWidget(parent), pixmap_(QStringLiteral(":/decorations/%1.png").arg(asset)),
          rotation_(rotation)
    {
        setObjectName(QStringLiteral("handdrawnAccent_%1").arg(asset));
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setFocusPolicy(Qt::NoFocus);
        setAccessibleName(QString());
        setAccessibleDescription(QString());
        setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    }
protected:
    void paintEvent(QPaintEvent *) override
    {
        if (pixmap_.isNull()) return;
        QPainter painter(this);
        painter.setRenderHint(QPainter::SmoothPixmapTransform);
        painter.translate(width() / 2.0, height() / 2.0);
        painter.rotate(rotation_);
        // Rotation happens inside an inset box so the full irregular outline fits.
        QSizeF size = pixmap_.size();
        size.scale(QSizeF(width() * .84, height() * .84), Qt::KeepAspectRatio);
        painter.drawPixmap(QRectF(-size.width() / 2, -size.height() / 2,
                                  size.width(), size.height()), pixmap_, pixmap_.rect());
    }
private:
    QPixmap pixmap_;
    qreal rotation_;
};
}

DecorativeHeading::DecorativeHeading(QWidget *text, const QString &asset, int extent,
                                   qreal rotation, QWidget *parent)
    : QWidget(parent), extent_(extent)
{
    initializeDecorations();
    setObjectName(QStringLiteral("decorativeHeading_%1").arg(asset));
    setStyleSheet(QStringLiteral("background: transparent;"));
    setMinimumWidth(0);
    auto *row = new QHBoxLayout(this);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(12);
    row->addWidget(text, 1);
    art_ = new Accent(asset, rotation, this);
    art_->setFixedSize(extent_, extent_);
    row->addWidget(art_, 0, Qt::AlignVCenter);
}

void DecorativeHeading::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    const int extent = qBound(40, event->size().width() / 4, extent_);
    art_->setFixedSize(extent, extent);
    // Very small embedded cards give all available space back to their text.
    art_->setVisible(event->size().width() >= 260);
}

} // namespace charging::client
