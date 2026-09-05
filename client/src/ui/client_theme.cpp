#include "ui/client_theme.h"

#include <QColor>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>

namespace charging::client {
namespace {

QPixmap navigationPixmap(NavigationIcon icon,
                         const QColor &color,
                         bool selected)
{
    constexpr int kCanvasSize = 64;
    QPixmap pixmap(kCanvasSize, kCanvasSize);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);

    QColor strokeColor = color;
    if (icon == NavigationIcon::Scan && selected) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(color);
        painter.drawEllipse(QRectF(2, 2, 60, 60));
        strokeColor = QColor(QStringLiteral("#ffffff"));
    }

    QPen pen(strokeColor, 4.2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);

    switch (icon) {
    case NavigationIcon::Charging: {
        QPainterPath bolt;
        bolt.moveTo(35, 5);
        bolt.lineTo(16, 34);
        bolt.lineTo(29, 34);
        bolt.lineTo(24, 59);
        bolt.lineTo(49, 27);
        bolt.lineTo(35, 27);
        bolt.closeSubpath();
        painter.setPen(Qt::NoPen);
        painter.setBrush(strokeColor);
        painter.drawPath(bolt);
        break;
    }
    case NavigationIcon::Orders:
        painter.drawRoundedRect(QRectF(14, 7, 36, 50), 5, 5);
        painter.drawLine(QPointF(22, 20), QPointF(42, 20));
        painter.drawLine(QPointF(22, 31), QPointF(42, 31));
        painter.drawLine(QPointF(22, 42), QPointF(36, 42));
        break;
    case NavigationIcon::Scan:
        painter.drawLine(QPointF(13, 25), QPointF(13, 13));
        painter.drawLine(QPointF(13, 13), QPointF(25, 13));
        painter.drawLine(QPointF(39, 13), QPointF(51, 13));
        painter.drawLine(QPointF(51, 13), QPointF(51, 25));
        painter.drawLine(QPointF(13, 39), QPointF(13, 51));
        painter.drawLine(QPointF(13, 51), QPointF(25, 51));
        painter.drawLine(QPointF(39, 51), QPointF(51, 51));
        painter.drawLine(QPointF(51, 51), QPointF(51, 39));
        painter.drawRect(QRectF(25, 25, 7, 7));
        painter.drawRect(QRectF(36, 25, 4, 4));
        painter.drawRect(QRectF(34, 36, 7, 7));
        break;
    case NavigationIcon::Support: {
        QPainterPath bubble;
        bubble.addRoundedRect(QRectF(9, 11, 46, 35), 10, 10);
        bubble.moveTo(23, 46);
        bubble.lineTo(18, 56);
        bubble.lineTo(32, 46);
        painter.drawPath(bubble);
        painter.setBrush(strokeColor);
        painter.setPen(Qt::NoPen);
        painter.drawEllipse(QRectF(20, 26, 5, 5));
        painter.drawEllipse(QRectF(30, 26, 5, 5));
        painter.drawEllipse(QRectF(40, 26, 5, 5));
        break;
    }
    case NavigationIcon::Profile:
        painter.drawEllipse(QRectF(23, 8, 18, 18));
        painter.drawArc(QRectF(13, 29, 38, 29), 20 * 16, 140 * 16);
        break;
    }

    return pixmap;
}

}  // namespace

QString clientThemeStyleSheet()
{
    return QStringLiteral(R"QSS(
QMainWindow, QWidget {
    background-color: #f5f7fb;
    color: #1d2939;
}

QLabel {
    background: transparent;
}

QScrollArea, QScrollArea > QWidget > QWidget {
    background: transparent;
    border: none;
}

QLineEdit, QComboBox {
    min-height: 38px;
    padding: 0 11px;
    color: #1d2939;
    background: #ffffff;
    border: 1px solid #cfd7e3;
    border-radius: 8px;
    selection-background-color: #1677ff;
}

QLineEdit:hover, QComboBox:hover {
    border-color: #98a6b8;
}

QLineEdit:focus, QComboBox:focus {
    border: 2px solid #1677ff;
    padding: 0 10px;
}

QLineEdit:disabled, QComboBox:disabled {
    color: #98a2b3;
    background: #eef1f5;
    border-color: #dde3ea;
}

QPushButton {
    min-height: 38px;
    padding: 0 15px;
    color: #344054;
    background: #ffffff;
    border: 1px solid #cfd7e3;
    border-radius: 8px;
    font-weight: 600;
}

QPushButton:hover {
    color: #155eef;
    background: #f5f9ff;
    border-color: #84adff;
}

QPushButton:pressed {
    background: #e8f1ff;
}

QPushButton:disabled {
    color: #98a2b3;
    background: #eef1f5;
    border-color: #dde3ea;
}

QPushButton:flat {
    min-height: 32px;
    padding: 0 6px;
    color: #155eef;
    background: transparent;
    border: none;
}

QPushButton:flat:hover {
    color: #004eeb;
    background: #eaf2ff;
}

QPushButton#loginButton,
QPushButton#stationRefreshButton,
QPushButton#scanStartButton,
QPushButton#rechargeButton,
QPushButton#saveNicknameButton,
QPushButton#orderDetailReservationScanButton,
QPushButton#orderDetailPayButton {
    color: #ffffff;
    background: #1677ff;
    border-color: #1677ff;
}

QPushButton#loginButton:hover,
QPushButton#stationRefreshButton:hover,
QPushButton#scanStartButton:hover,
QPushButton#rechargeButton:hover,
QPushButton#saveNicknameButton:hover,
QPushButton#orderDetailReservationScanButton:hover,
QPushButton#orderDetailPayButton:hover {
    background: #0958d9;
    border-color: #0958d9;
}

QPushButton#loginButton:disabled,
QPushButton#stationRefreshButton:disabled,
QPushButton#scanStartButton:disabled,
QPushButton#rechargeButton:disabled,
QPushButton#saveNicknameButton:disabled,
QPushButton#orderDetailReservationScanButton:disabled,
QPushButton#orderDetailPayButton:disabled {
    color: #ffffff;
    background: #9fc5f8;
    border-color: #9fc5f8;
}

QPushButton#logoutButton,
QPushButton#orderDetailCancelButton,
QPushButton#orderDetailStopButton {
    color: #b42318;
    background: #fff7f6;
    border-color: #f2b8b5;
}

QPushButton#logoutButton:hover,
QPushButton#orderDetailCancelButton:hover,
QPushButton#orderDetailStopButton:hover {
    background: #feeceb;
    border-color: #e6807b;
}

QPushButton#currentOrderNavigationButton,
QPushButton#orderDetailNavigationButton {
    color: #155eef;
    background: #eaf2ff;
    border-color: #b2ccff;
}

QPushButton#currentOrderNavigationButton:hover,
QPushButton#orderDetailNavigationButton:hover {
    color: #004eeb;
    background: #dbeafe;
    border-color: #84adff;
}

QCheckBox {
    spacing: 8px;
    color: #475467;
}

QTabWidget#mainNavigation::pane {
    border: none;
}

QTabWidget#mainNavigation > QTabBar {
    background: #ffffff;
    border-top: 1px solid #e4e7ec;
}

QTabWidget#mainNavigation QTabBar::tab {
    min-height: 48px;
    padding: 0 4px;
    color: #667085;
    background: #ffffff;
    border: none;
    border-top: 3px solid transparent;
    font-weight: 500;
}

QTabWidget#mainNavigation QTabBar::tab:hover {
    color: #155eef;
    background: #f5f9ff;
}

QTabWidget#mainNavigation QTabBar::tab:selected {
    color: #155eef;
    background: #f5f9ff;
    border-top-color: #1677ff;
    font-weight: 700;
}

QFrame#supportCard {
    background: #ffffff;
    border: 1px solid #e4e7ec;
    border-radius: 16px;
}

QLabel#supportBadge {
    padding: 5px 10px;
    color: #155eef;
    background: #eaf2ff;
    border-radius: 10px;
    font-size: 12px;
    font-weight: 600;
}

QToolTip {
    padding: 6px 8px;
    color: #ffffff;
    background: #344054;
    border: none;
    border-radius: 5px;
}
)QSS");
}

QIcon clientNavigationIcon(NavigationIcon icon)
{
    QIcon result;
    result.addPixmap(navigationPixmap(icon,
                                      QColor(QStringLiteral("#667085")),
                                      false),
                     QIcon::Normal,
                     QIcon::Off);
    result.addPixmap(navigationPixmap(icon,
                                      QColor(QStringLiteral("#155eef")),
                                      false),
                     QIcon::Active,
                     QIcon::Off);
    result.addPixmap(navigationPixmap(icon,
                                      QColor(QStringLiteral("#1677ff")),
                                      true),
                     QIcon::Selected,
                     QIcon::Off);
    result.addPixmap(navigationPixmap(icon,
                                      QColor(QStringLiteral("#98a2b3")),
                                      false),
                     QIcon::Disabled,
                     QIcon::Off);
    return result;
}

}  // namespace charging::client
