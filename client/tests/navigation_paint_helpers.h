#pragma once

#include <QDir>
#include <QImage>
#include <QScreen>
#include <QTabBar>
#include <QtTest>

namespace charging::client::navigation_test {

// Read presented pixels: QWidget::grab()/render() repaints the whole widget
// and hides the disjoint-dirty-region corruption this regression checks.
inline QImage presentedNavigation(QTabBar &bar, const QString &stage)
{
    QTest::qWait(60);
    QWidget *window = bar.window();
    const auto snapshot = window->screen()->grabWindow(
        window->winId(), 0, 0, window->width(), window->height());
    // Some platform plugins (notably Qt 6.2/6.4 offscreen at non-unit scale)
    // truncate captures to logical bounds. Do not mistake a cropped screenshot
    // for missing UI; use X11/Xvfb to verify those high-DPI configurations.
    const QSize nativeSize = window->size() * window->devicePixelRatioF();
    if (snapshot.width() < nativeSize.width() || snapshot.height() < nativeSize.height())
        return {};
    const auto directory = qEnvironmentVariable("CHARGING_NAVIGATION_SCREENSHOTS");
    if (!directory.isEmpty() && !snapshot.isNull()) {
        const auto name = QStringLiteral("%1-%2x%3.png")
                              .arg(stage).arg(window->width()).arg(window->height());
        snapshot.save(QDir(directory).filePath(name));
    }
    const auto image = snapshot.toImage().scaled(window->size(), Qt::IgnoreAspectRatio,
                                                Qt::FastTransformation);
    if (image.isNull()) return {};
    return image.copy(QRect(bar.mapTo(window, QPoint()), bar.size()));
}

inline QString missingNavigationContent(const QTabBar &bar, const QImage &image)
{
    if (image.isNull() || image.size() != bar.size()) {
        return QStringLiteral("Cannot capture the complete navigation bar");
    }
    for (int index = 0; index < bar.count(); ++index) {
        const QRect tab = bar.tabRect(index);
        // Sample icon and label separately, excluding the tile border/shadow.
        const QRect areas[] = {
            QRect(tab.center().x() - 14, 16, 28, 35),
            QRect(tab.left() + 2, 53, tab.width() - 4, 28)
        };
        for (int part = 0; part < 2; ++part) {
            int ink = 0;
            for (int y = areas[part].top(); y <= areas[part].bottom(); ++y) {
                for (int x = areas[part].left(); x <= areas[part].right(); ++x) {
                    if (!image.rect().contains(x, y)) continue;
                    const auto color = image.pixelColor(x, y);
                    if (color.red() < 160 && color.green() < 175 && color.blue() < 165)
                        ++ink;
                }
            }
            if (ink < 10) {
                return QStringLiteral("Tab %1 (%2) lost its %3: only %4 ink pixels remain")
                    .arg(index).arg(bar.tabText(index), part == 0 ? QStringLiteral("icon")
                                                                 : QStringLiteral("label"))
                    .arg(ink);
            }
        }
    }
    return {};
}

}  // namespace charging::client::navigation_test
