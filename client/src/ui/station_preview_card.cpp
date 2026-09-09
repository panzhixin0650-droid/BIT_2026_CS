#include "ui/station_preview_card.h"

#include "ui/client_theme.h"
#include <QHBoxLayout>
#include <QGraphicsDropShadowEffect>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

// 本文件实现地图上的电站预览卡片：概要信息与两个操作按钮
namespace charging::client {

// 构造卡片布局：名称、地址、指标、预测与操作按钮
StationPreviewCard::StationPreviewCard(QWidget *parent) : QFrame(parent)
{
    setObjectName(QStringLiteral("stationPreviewCard"));
    setProperty("role", "mapCard");
    auto *shadow = new QGraphicsDropShadowEffect(this);
    shadow->setBlurRadius(14);
    shadow->setOffset(0, 2);
    shadow->setColor(QColor(32, 61, 48, 20));
    setGraphicsEffect(shadow);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(16, 12, 16, 12);
    layout->setSpacing(7);
    auto *title = new QHBoxLayout;
    name_ = new QLabel(this);
    name_->setObjectName(QStringLiteral("stationPreviewName"));
    name_->setTextFormat(Qt::PlainText);
    name_->setWordWrap(true);
    name_->setMaximumHeight(48);
    name_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    auto *close = new QPushButton(QStringLiteral("×"), this);
    close->setObjectName(QStringLiteral("stationPreviewClose"));
    close->setProperty("role", "mapDismiss");
    close->setFixedSize(44, 44);
    close->setAccessibleName(QStringLiteral("返回推荐电站"));
    close->setToolTip(close->accessibleName());
    title->addWidget(name_, 1);
    title->addWidget(close, 0, Qt::AlignTop);
    address_ = new QLabel(this);
    address_->setObjectName(QStringLiteral("stationPreviewAddress"));
    address_->setTextFormat(Qt::PlainText);
    address_->setWordWrap(true);
    address_->setMaximumHeight(36);
    metrics_ = new QLabel(this);
    metrics_->setObjectName(QStringLiteral("stationPreviewMetrics"));
    metrics_->setWordWrap(true);
    prediction_ = new QLabel(this);
    prediction_->setObjectName(QStringLiteral("stationPreviewPrediction"));
    prediction_->setWordWrap(true);
    auto *actions = new QHBoxLayout;
    auto *details = new QPushButton(QStringLiteral("选桩充电"), this);
    details->setObjectName(QStringLiteral("stationPreviewDetailsButton"));
    details->setProperty("role", "primary");
    details->setMinimumHeight(44);
    auto *navigate = new QPushButton(QStringLiteral("导航到站"), this);
    navigate->setObjectName(QStringLiteral("stationPreviewNavigationButton"));
    navigate->setMinimumHeight(44);
    navigate->setIcon(clientNavigationIcon(NavigationIcon::Route));
    actions->addWidget(navigate, 1);
    actions->addWidget(details, 1);
    layout->addLayout(title);
    layout->addWidget(address_);
    layout->addWidget(metrics_);
    layout->addWidget(prediction_);
    layout->addStretch(1);
    layout->addLayout(actions);
    // 按钮只发出信号，跳转与导航由外部页面处理
    connect(close, &QPushButton::clicked, this, &StationPreviewCard::dismissed);
    connect(details, &QPushButton::clicked, this, [this] {
        emit detailsRequested(station_.stationId);
    });
    connect(navigate, &QPushButton::clicked, this, [this] {
        emit navigationRequested(station_);
    });
    hide();
}

// 填充电站数据，无空闲桩时按钮改为查看电桩
void StationPreviewCard::setStation(const protocol::StationDto &station)
{
    station_ = station;
    setProperty("stationId", station.stationId);
    name_->setText(station.name);
    name_->setToolTip(station.name);
    address_->setText(station.address.isEmpty() ? QStringLiteral("地址暂未提供") : station.address);
    address_->setToolTip(address_->text());
    auto *details = findChild<QPushButton *>(QStringLiteral("stationPreviewDetailsButton"));
    details->setText(station.status == protocol::StationStatus::Active && station.availablePileCount > 0
                         ? QStringLiteral("选桩充电") : QStringLiteral("查看电桩"));
    // 距离、空闲数与单价拼成一行，金额由整数分换算
    const QString distance = station.distanceKm
        ? QStringLiteral("%1 km").arg(*station.distanceKm, 0, 'f', 2)
        : QStringLiteral("距离待定位");
    metrics_->setText(QStringLiteral("%1 · %2/%3 空闲 · ¥%4.%5/度")
        .arg(distance).arg(station.availablePileCount).arg(station.totalPileCount)
        .arg(station.priceCentsPerKwh / 100)
        .arg(station.priceCentsPerKwh % 100, 2, 10, QLatin1Char('0')));
    // 拥堵预测缺失时显示暂不可用，推荐站点额外标注
    QString prediction = QStringLiteral("拥堵预测暂不可用");
    if (station.predictedCongestion) {
        switch (*station.predictedCongestion) {
        case protocol::CongestionLevel::Low: prediction = QStringLiteral("预计低拥堵"); break;
        case protocol::CongestionLevel::Medium: prediction = QStringLiteral("预计一般拥堵"); break;
        case protocol::CongestionLevel::High: prediction = QStringLiteral("预计高拥堵"); break;
        }
    }
    prediction_->setText(prediction + (station.recommended ? QStringLiteral(" · 推荐站点") : QString{}));
}

}  // namespace charging::client
