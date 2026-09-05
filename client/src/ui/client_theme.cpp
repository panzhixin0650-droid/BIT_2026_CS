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
    if (icon == NavigationIcon::Scan) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(selected ? "#d5eab1" : "#245c45"));
        painter.drawEllipse(QRectF(2, 2, 60, 60));
        strokeColor = QColor(selected ? "#245c45" : "#ffffff");
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
    case NavigationIcon::Route: {
        QPainterPath arrow;
        arrow.moveTo(12, 29);
        arrow.lineTo(52, 11);
        arrow.lineTo(35, 53);
        arrow.lineTo(30, 34);
        arrow.closeSubpath();
        painter.drawPath(arrow);
        break;
    }
    }

    return pixmap;
}

}  // namespace

QString clientThemeStyleSheet()
{
    return QStringLiteral(R"QSS(
QMainWindow, QWidget {
    background-color: #f6f7f2;
    color: #203d33;
    font-family: "Noto Sans CJK SC", "Noto Sans", sans-serif;
}

QLabel {
    background: transparent;
}

QScrollArea, QScrollArea > QWidget > QWidget {
    background: transparent;
    border: none;
}

QWidget#applicationPages, QWidget#qt_tabwidget_stackedwidget {
    background: #f6f7f2;
}

QLabel[role="eyebrow"] {
    color: #65796c;
    font-size: 10px;
    font-weight: 600;
}

QLabel[role="sectionTitle"] {
    font-size: 18px;
    font-weight: 700;
}

QFrame[role="card"] {
    background: #ffffff;
    border: 1px solid #e1e7dc;
    border-radius: 18px;
}

QFrame[role="card"] QLabel { background: transparent; border: none; }

QPushButton[role="primary"], QPushButton#routePlanButton,
QPushButton#startReservedChargingButton {
    color: white; background: #245c45; border-color: #245c45;
}
QPushButton[role="primary"]:hover, QPushButton#routePlanButton:hover,
QPushButton#startReservedChargingButton:hover { background: #163f31; }
QPushButton[role="primary"]:disabled, QPushButton#routePlanButton:disabled,
QPushButton#startReservedChargingButton:disabled {
    color: #f2f5ef; background: #95ae9e; border-color: #95ae9e;
}

QScrollBar:vertical { background: transparent; width: 6px; margin: 3px 0; }
QScrollBar::handle:vertical { background: #cad5c7; border-radius: 3px; min-height: 32px; }
QScrollBar::handle:vertical:hover { background: #96ad98; }
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }
QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: transparent; }

QLineEdit, QComboBox {
    min-height: 38px;
    padding: 0 11px;
    color: #203d33;
    background: #ffffff;
    border: 1px solid #dce3d5;
    border-radius: 11px;
    selection-background-color: #245c45;
}

QLineEdit:hover, QComboBox:hover {
    border-color: #9cab94;
}

QLineEdit:focus, QComboBox:focus {
    border: 2px solid #245c45;
    padding: 0 10px;
}

QLineEdit:disabled, QComboBox:disabled {
    color: #96a18e;
    background: #edf0e8;
    border-color: #e1e6da;
}

QPushButton {
    min-height: 38px;
    padding: 0 15px;
    color: #36523f;
    background: #ffffff;
    border: 1px solid #dce3d5;
    border-radius: 11px;
    font-weight: 600;
}

QPushButton:hover {
    color: #245c45;
    background: #f0f5e9;
    border-color: #92ad7e;
}

QPushButton:pressed {
    background: #e4eedb;
}

QPushButton:focus { border: 2px solid #567b52; padding: 0 14px; }
QPushButton[rechargeAmount] { min-width: 0; padding: 0 8px; }
QPushButton[rechargeAmount]:checked {
    background: #d8e9c3; color: #21472f; border-color: #6d9259;
}

QPushButton:disabled {
    color: #96a18e;
    background: #edf0e8;
    border-color: #e1e6da;
}

QPushButton:flat {
    min-height: 32px;
    padding: 0 6px;
    color: #245c45;
    background: transparent;
    border: none;
}

QPushButton:flat:hover {
    color: #163f31;
    background: #edf4e4;
}

QPushButton#loginButton,
QPushButton#stationRefreshButton,
QPushButton#scanStartButton,
QPushButton#rechargeButton,
QPushButton#saveNicknameButton,
QPushButton#orderDetailReservationScanButton,
QPushButton#orderDetailPayButton {
    color: #ffffff;
    background: #245c45;
    border-color: #245c45;
}

QPushButton#loginButton:hover,
QPushButton#stationRefreshButton:hover,
QPushButton#scanStartButton:hover,
QPushButton#rechargeButton:hover,
QPushButton#saveNicknameButton:hover,
QPushButton#orderDetailReservationScanButton:hover,
QPushButton#orderDetailPayButton:hover {
    background: #163f31;
    border-color: #163f31;
}

QPushButton#loginButton:disabled,
QPushButton#stationRefreshButton:disabled,
QPushButton#scanStartButton:disabled,
QPushButton#rechargeButton:disabled,
QPushButton#saveNicknameButton:disabled,
QPushButton#orderDetailReservationScanButton:disabled,
QPushButton#orderDetailPayButton:disabled {
    color: #ffffff;
    background: #9ab3a1;
    border-color: #9ab3a1;
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
    color: #245c45;
    background: #edf4e4;
    border-color: #bfd3ac;
}

QPushButton#currentOrderNavigationButton:hover,
QPushButton#orderDetailNavigationButton:hover {
    color: #163f31;
    background: #dce9cc;
    border-color: #92ad7e;
}

QCheckBox {
    spacing: 8px;
    color: #536553;
}

QCheckBox::indicator { width: 16px; height: 16px; border: 1px solid #a5b4a3;
    border-radius: 5px; background: white; }
QCheckBox::indicator:checked { background: #245c45; border: 3px solid #c9ddba; }
QCheckBox::indicator:disabled { background: #dce2d7; border-color: #c5cec0; }

QTabWidget#mainNavigation::pane {
    border: none;
}

QTabWidget#mainNavigation > QTabBar {
    background: #ffffff;
    border-top: 1px solid #e1e7dc;
}

QTabWidget#mainNavigation QTabBar::tab {
    min-height: 48px;
    padding: 0 4px;
    color: #697969;
    background: #ffffff;
    border: none;
    border-top: 2px solid transparent;
    font-weight: 500;
}

QTabWidget#mainNavigation QTabBar::tab:hover {
    color: #245c45;
    background: #f0f5e9;
}

QTabWidget#mainNavigation QTabBar::tab:selected {
    color: #245c45;
    background: #f0f5e9;
    border-top-color: #245c45;
    font-weight: 700;
}

QFrame#supportCard {
    background: #ffffff;
    border: 1px solid #e1e7dc;
    border-radius: 16px;
}

QLabel#supportBadge {
    padding: 5px 10px;
    color: #245c45;
    background: #edf4e4;
    border-radius: 10px;
    font-size: 12px;
    font-weight: 600;
}

QToolTip {
    padding: 6px 8px;
    color: #ffffff;
    background: #36523f;
    border: none;
    border-radius: 5px;
}
)QSS");
}

QIcon clientNavigationIcon(NavigationIcon icon)
{
    QIcon result;
    result.addPixmap(navigationPixmap(icon,
                                      QColor(QStringLiteral("#697969")),
                                      false),
                     QIcon::Normal,
                     QIcon::Off);
    result.addPixmap(navigationPixmap(icon,
                                      QColor(QStringLiteral("#245c45")),
                                      false),
                     QIcon::Active,
                     QIcon::Off);
    result.addPixmap(navigationPixmap(icon,
                                      QColor(QStringLiteral("#245c45")),
                                      true),
                     QIcon::Selected,
                     QIcon::Off);
    result.addPixmap(navigationPixmap(icon,
                                      QColor(QStringLiteral("#96a18e")),
                                      false),
                     QIcon::Disabled,
                     QIcon::Off);
    return result;
}

}  // namespace charging::client
