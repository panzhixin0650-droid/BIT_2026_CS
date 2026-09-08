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
    Q_UNUSED(selected)

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
    case NavigationIcon::Repair: {
        QPainterPath wrench;
        wrench.moveTo(39, 10);
        wrench.cubicTo(26, 5, 19, 18, 24, 28);
        wrench.lineTo(9, 43);
        wrench.cubicTo(3, 50, 14, 61, 21, 54);
        wrench.lineTo(37, 38);
        wrench.cubicTo(49, 43, 60, 31, 54, 20);
        wrench.lineTo(43, 30);
        wrench.lineTo(34, 21);
        wrench.closeSubpath();
        painter.drawPath(wrench);
        break;
    }
    case NavigationIcon::Tickets:
        painter.drawRoundedRect(QRectF(10, 12, 44, 42), 5, 5);
        painter.drawLine(QPointF(21, 23), QPointF(43, 23));
        painter.drawLine(QPointF(21, 32), QPointF(34, 32));
        painter.drawLine(QPointF(22, 42), QPointF(28, 47));
        painter.drawLine(QPointF(28, 47), QPointF(42, 37));
        break;
    case NavigationIcon::ChevronRight:
        painter.drawLine(QPointF(25, 15), QPointF(42, 32));
        painter.drawLine(QPointF(42, 32), QPointF(25, 49));
        break;
    case NavigationIcon::Wallet:
        painter.drawRoundedRect(QRectF(8, 16, 48, 37), 6, 6);
        painter.drawLine(QPointF(12, 16), QPointF(43, 8));
        painter.drawLine(QPointF(43, 8), QPointF(47, 16));
        painter.drawRoundedRect(QRectF(37, 28, 19, 14), 3, 3);
        painter.drawPoint(QPointF(44, 35));
        break;
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

QWidget#stationHomeOverlay, QWidget#stationMapControls,
QWidget#currentOrderDetails { background: transparent; }
QFrame[role="mapCard"] {
    background: rgba(255, 254, 250, 246);
    border: 1px solid #dce5d7;
    border-radius: 18px;
}
QFrame[role="mapCard"] QLabel { background: transparent; border: none; }
QLabel#stationHomeBrand { font-size: 12px; font-weight: 700; color: #245c45; }
QLabel#stationSearchIcon { font-size: 23px; color: #65796c; }
QLabel#welcomeLabel, QLabel#stationLocationCaption, QLabel#stationResultCount {
    font-size: 11px; color: #566f5f;
}
QLabel#stationMapMode { font-size: 10px; color: #65796c; }
QLabel#stationMapStatus { background: #fffefa; border-radius: 16px; padding: 12px; }
QLineEdit#stationKeywordInput { min-height: 34px; padding: 0 6px; border: none; background: transparent; font-size: 12px; }
QLineEdit#stationKeywordInput:focus { border: 1px solid #91ab92; border-radius: 10px; }
QPushButton#stationRefreshButton, QPushButton#stationFilterToggle {
    min-height: 34px; padding: 0; font-size: 11px; border-radius: 10px;
}
QPushButton#stationFilterToggle:checked { background: #e3eedb; border-color: #91ab92; }
QScrollArea#stationFilterScrollArea, QWidget#stationAdvancedFilters {
    background: #fffefa; border-radius: 16px;
}
QWidget#stationAdvancedFilters QLabel { font-size: 11px; }
QFrame#currentOrderCard { background: #edf4e5; border: 1px solid #c8d8bd; border-radius: 16px; }
QPushButton#currentOrderToggle { text-align: left; min-height: 32px; padding: 0 4px; font-size: 12px; }
QWidget#currentOrderDetails QPushButton { padding: 0 7px; min-height: 34px; font-size: 11px; }
QLabel#currentOrderSummary, QLabel#currentOrderProgress { font-size: 12px; color: #36583c; }
QLabel#stationPreviewName { font-size: 17px; font-weight: 700; color: #203d33; }
QLabel#stationPreviewMetrics { font-size: 12px; color: #245c45; font-weight: 600; }
QLabel#stationPreviewPrediction { font-size: 11px; color: #65796c; }
QPushButton[role="mapControl"] {
    min-height: 0; padding: 0; background: #fffefa;
    border: 1px solid #d7e1d1; border-radius: 20px; font-size: 21px;
}
QPushButton[role="mapControl"]:hover { background: #e3eedb; }
QPushButton[role="mapControl"]:focus { padding: 0; border: 2px solid #567b52; }
QPushButton[role="mapDismiss"] { min-height: 0; padding: 0; border: none; background: transparent; font-size: 20px; }
QPushButton[role="mapDismiss"]:hover { background: #e3eedb; }

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

QTabWidget#mainNavigation::tab-bar {
    alignment: center;
}

QFrame#navigationContainer {
    background: #ffffff;
    border: 1px solid #e5e9e2;
    border-radius: 20px;
}

QTabWidget#mainNavigation > QTabBar {
    background: transparent;
    border: none;
    margin: 0;
    padding: 0;
}

QTabWidget#mainNavigation QTabBar::tab {
    padding: 0;
    margin: 0;
    color: #718078;
    background: transparent;
    border: none;
    border-radius: 12px;
    font-weight: 500;
}

QTabWidget#mainNavigation QTabBar::tab:hover {
    color: #6f927a;
}

QTabWidget#mainNavigation QTabBar::tab:selected {
    color: #466b53;
    background: #eaf1e8;
    border: none;
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

QString profileThemeStyleSheet()
{
    // Keep Profile rules local so unrelated pages do not inherit their style metrics.
    return QStringLiteral(R"QSS(
QWidget#profilePage, QWidget#profileScrollContent, QWidget#profileContent {
    background: #f7f8f3;
    color: #24372d;
}
QWidget#profilePage QLabel { color: #24372d; }
QWidget#profilePage QLabel[role="profileSecondary"] { color: #718078; font-size: 12px; }
QWidget#profilePage QLabel[role="profileSection"] { font-size: 14px; font-weight: 600; }
QWidget#profilePage QFrame[role="card"] {
    background: #ffffff; border: 1px solid #e5e9e2; border-radius: 16px;
}
QWidget#profilePage QFrame#profileIdentityCard {
    background: #eaf1e8; border: none;
}
QWidget#profilePage QLabel#profileAvatar {
    background: #dce9b5; color: #466b53; border-radius: 34px; font-size: 16px; font-weight: 600;
}
QWidget#profilePage QLabel#profileBalanceLabel { color: #365b44; }
QWidget#profilePage QWidget#profileEditor { background: transparent; }
QWidget#profilePage QFrame[role="profileDivider"] { background: #e5e9e2; border: none; }
QWidget#profilePage QPushButton, QWidget#profilePage QLineEdit {
    min-height: 38px; padding: 0 12px; border: 1px solid #e5e9e2; border-radius: 10px;
    color: #24372d; background: #ffffff; font-size: 13px;
    selection-background-color: #6f927a;
}
QWidget#profilePage QPushButton:hover, QWidget#profilePage QLineEdit:hover {
    background: #f5f7f2; border-color: #a8bca9;
}
QWidget#profilePage QPushButton:pressed { background: #dfeadb; }
QWidget#profilePage QPushButton:focus, QWidget#profilePage QLineEdit:focus {
    min-height: 36px; border: 2px solid #6f927a; padding: 0 11px;
}
QWidget#profilePage QPushButton[rechargeAmount] {
    min-height: 30px; padding: 0 4px; background: #f5f7f2; border-color: #e5e9e2; border-radius: 9px; font-weight: 500;
}
QWidget#profilePage QPushButton[rechargeAmount]:checked {
    color: #365b44; background: #eaf1e8; border-color: #6f927a; font-weight: 600;
}
QWidget#profilePage QPushButton[rechargeAmount]:hover { background: #eaf1e8; border-color: #a8bca9; }
QWidget#profilePage QPushButton[rechargeAmount]:focus { min-height: 28px; padding: 0 3px; border: 2px solid #6f927a; }
QWidget#profilePage QPushButton#rechargeButton {
    background: #6f927a; color: #ffffff; border-color: #6f927a;
}
QWidget#profilePage QPushButton#rechargeButton:hover { background: #5e8169; border-color: #5e8169; }
QWidget#profilePage QPushButton#saveNicknameButton {
    background: #eaf1e8; color: #466b53; border-color: #dce5d8;
}
QWidget#profilePage QPushButton#saveNicknameButton:hover { background: #dce9d8; }
QWidget#profilePage QPushButton:disabled, QWidget#profilePage QLineEdit:disabled {
    color: #93a097; background: #edf0e9; border-color: #e5e9e2;
}
QWidget#profilePage QPushButton#rechargeButton:disabled {
    color: #ffffff; background: #a6b9ab; border-color: #a6b9ab;
}
QWidget#profilePage QPushButton#saveNicknameButton:disabled {
    color: #93a097; background: #edf0e9; border-color: #e5e9e2;
}
QWidget#profilePage QPushButton:flat, QWidget#profilePage QPushButton[role="profileService"] {
    min-height: 30px; padding: 0 6px; background: transparent; border: 1px solid transparent;
    color: #718078; font-weight: 500;
}
QWidget#profilePage QPushButton[role="profileService"] { min-height: 50px; }
QWidget#profilePage QPushButton[role="profileService"] QLabel { font-size: 14px; }
QWidget#profilePage QPushButton:flat:hover,
QWidget#profilePage QPushButton[role="profileService"]:hover { background: #f0f5ed; }
QWidget#profilePage QPushButton:flat:focus,
QWidget#profilePage QPushButton[role="profileService"]:focus { border: 1px solid #6f927a; }
QWidget#profilePage QPushButton#logoutButton { min-height: 34px; color: #718078; }
QWidget#profilePage QPushButton#logoutButton:hover { color: #a5564e; background: #f7eeeb; }
)QSS");
}

QIcon clientNavigationIcon(NavigationIcon icon)
{
    QIcon result;
    result.addPixmap(navigationPixmap(icon,
                                      QColor(QStringLiteral("#718078")),
                                      false),
                     QIcon::Normal,
                     QIcon::Off);
    result.addPixmap(navigationPixmap(icon,
                                      QColor(QStringLiteral("#6f927a")),
                                      false),
                     QIcon::Active,
                     QIcon::Off);
    result.addPixmap(navigationPixmap(icon,
                                      QColor(QStringLiteral("#466b53")),
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
