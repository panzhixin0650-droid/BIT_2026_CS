#pragma once

#include "charging/protocol/dto.h"

#include <QFrame>

class QLabel;

namespace charging::client {

class StationPreviewCard final : public QFrame {
    Q_OBJECT
public:
    explicit StationPreviewCard(QWidget *parent = nullptr);
    void setStation(const protocol::StationDto &station);

signals:
    void detailsRequested(qint64 stationId);
    void navigationRequested(const protocol::StationDto &station);
    void dismissed();

private:
    protocol::StationDto station_;
    QLabel *name_;
    QLabel *address_;
    QLabel *metrics_;
    QLabel *prediction_;
};

}  // namespace charging::client
