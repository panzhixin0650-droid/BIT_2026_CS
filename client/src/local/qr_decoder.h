// 二维码识别接口声明，供扫码开始充电使用
#pragma once

#include <QImage>
#include <QString>

namespace charging::client {

// 识别结果：成功给桩编号，失败给中文提示
struct QrDecodeResult {
    QString pileCode;
    QString error;
    bool ok() const { return !pileCode.isEmpty(); }
};

// 分别支持从内存图像和图片文件识别
QrDecodeResult decodePileQr(const QImage &image);
QrDecodeResult decodePileQrFile(const QString &path);

} // namespace charging::client
