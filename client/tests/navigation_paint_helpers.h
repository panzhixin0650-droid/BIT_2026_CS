// 本文件为导航栏绘制测试提供截图与像素检查的公共辅助
#pragma once

#include <QDir>
#include <QImage>
#include <QScreen>
#include <QTabBar>
#include <QtTest>

namespace charging::client::navigation_test {

// Read presented pixels: QWidget::grab()/render() repaints the whole widget
// and hides the disjoint-dirty-region corruption this regression checks.
// 抓取窗口真实呈现的像素，再裁剪出导航栏区域
inline QImage presentedNavigation(QTabBar &bar, const QString &stage)
{
    QTest::qWait(60);
    QWidget *window = bar.window();
    const auto snapshot = window->screen()->grabWindow(
        window->winId(), 0, 0, window->width(), window->height());
    // Some platform plugins (notably Qt 6.2/6.4 offscreen at non-unit scale)
    // truncate captures to logical bounds. Do not mistake a cropped screenshot
    // for missing UI; use X11/Xvfb to verify those high-DPI configurations.
    // 截图小于原生尺寸说明平台裁剪，返回空图让调用方跳过
    const QSize nativeSize = window->size() * window->devicePixelRatioF();
    if (snapshot.width() < nativeSize.width() || snapshot.height() < nativeSize.height())
        return {};
    // 设置环境变量时保存截图，便于排查失败
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

// 逐个标签检查图标和文字区域是否还有内容，返回缺失说明
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
            // 深色像素太少即认为该图标或文字丢失
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
