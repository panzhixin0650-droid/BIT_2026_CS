#include "ui/client_theme.h"

namespace charging::client {

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

}  // namespace charging::client
