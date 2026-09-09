#pragma once

#include "charging/protocol/dto.h"

#include <QFrame>

class QLabel;

namespace charging::client {

// 本文件声明电站预览卡片控件
class StationPreviewCard final : public QFrame {
    Q_OBJECT
public:
    explicit StationPreviewCard(QWidget *parent = nullptr);
    // 传入电站数据即刷新卡片显示
    void setStation(const protocol::StationDto &station);

// 信号：请求选桩详情、导航或关闭卡片
signals:
    void detailsRequested(qint64 stationId);
    void navigationRequested(const protocol::StationDto &station);
    void dismissed();

private:
    // 缓存当前电站，供导航信号原样带出
    protocol::StationDto station_;
    QLabel *name_;
    QLabel *address_;
    QLabel *metrics_;
    QLabel *prediction_;
};

}  // namespace charging::client
