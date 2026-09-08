#pragma once

#include <QImage>
#include <QString>

namespace charging::client {

struct QrDecodeResult {
    QString pileCode;
    QString error;
    bool ok() const { return !pileCode.isEmpty(); }
};

QrDecodeResult decodePileQr(const QImage &image);
QrDecodeResult decodePileQrFile(const QString &path);

} // namespace charging::client
