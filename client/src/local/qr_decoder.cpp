#include "local/qr_decoder.h"

#include <QFileInfo>
#include <QImageReader>
#include <QRegularExpression>
#include <QSet>
#include <zbar.h>

#include <cstring>
#include <memory>

namespace charging::client {

QrDecodeResult decodePileQr(const QImage &source)
{
    if (source.isNull()) return {{}, QStringLiteral("无法读取图片，请选择 PNG 或 JPEG 图片")};
    QImage gray = source;
    if (gray.width() > 1600 || gray.height() > 1600)
        gray = gray.scaled(1600, 1600, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    gray = gray.convertToFormat(QImage::Format_Grayscale8);
    // ZBar expects packed rows, while QImage scanlines can contain padding.
    QByteArray pixels(gray.width() * gray.height(), Qt::Uninitialized);
    for (int y = 0; y < gray.height(); ++y)
        std::memcpy(pixels.data() + y * gray.width(), gray.constScanLine(y), gray.width());

    using namespace zbar;
    std::unique_ptr<zbar_image_scanner_t, decltype(&zbar_image_scanner_destroy)> scanner(
        zbar_image_scanner_create(), zbar_image_scanner_destroy);
    std::unique_ptr<zbar_image_t, decltype(&zbar_image_destroy)> image(
        zbar_image_create(), zbar_image_destroy);
    if (!scanner || !image) return {{}, QStringLiteral("二维码识别暂不可用，请重试")};
    zbar_image_scanner_set_config(scanner.get(), ZBAR_NONE, ZBAR_CFG_ENABLE, 0);
    zbar_image_scanner_set_config(scanner.get(), ZBAR_QRCODE, ZBAR_CFG_ENABLE, 1);
    zbar_image_set_format(image.get(), zbar_fourcc('Y', '8', '0', '0'));
    zbar_image_set_size(image.get(), gray.width(), gray.height());
    zbar_image_set_data(image.get(), pixels.constData(), pixels.size(), nullptr);
    if (zbar_scan_image(scanner.get(), image.get()) < 0)
        return {{}, QStringLiteral("二维码识别失败，请换一张图片或重试")};

    QSet<QString> codes;
    for (auto *symbol = zbar_image_first_symbol(image.get()); symbol;
         symbol = zbar_symbol_next(symbol)) {
        if (zbar_symbol_get_type(symbol) != ZBAR_QRCODE) continue;
        const auto length = zbar_symbol_get_data_length(symbol);
        if (length > 256) return {{}, QStringLiteral("二维码不是有效的充电桩编号")};
        codes.insert(QString::fromUtf8(zbar_symbol_get_data(symbol), length).trimmed());
    }
    if (codes.isEmpty()) return {{}, QStringLiteral("未发现二维码，请对准完整二维码或选择更清晰的图片")};
    if (codes.size() != 1) return {{}, QStringLiteral("发现多个不同二维码，请只保留一个充电桩二维码")};
    const QString code = *codes.cbegin();
    // The local scanner accepts a plain pile code, never executes/opens QR URLs.
    static const QRegularExpression pattern(QStringLiteral("^[A-Za-z0-9][A-Za-z0-9_.-]{0,63}$"));
    if (!pattern.match(code).hasMatch())
        return {{}, QStringLiteral("请扫描包含桩编号的二维码，例如 PILE-A-01；暂不支持网址二维码")};
    return {code, {}};
}

QrDecodeResult decodePileQrFile(const QString &path)
{
    const QFileInfo file(path);
    if (!file.isFile() || file.size() > 20 * 1024 * 1024)
        return {{}, QStringLiteral("请选择不超过 20 MB 的二维码图片")};
    QImageReader reader(path);
    reader.setDecideFormatFromContent(true);
    reader.setAutoTransform(true);
    const QSize size = reader.size();
    if (!size.isValid() || qint64(size.width()) * size.height() > 32000000)
        return {{}, QStringLiteral("图片无效或尺寸过大，请选择不超过 3200 万像素的图片")};
    if (size.width() > 1600 || size.height() > 1600)
        reader.setScaledSize(size.scaled(1600, 1600, Qt::KeepAspectRatio));
    return decodePileQr(reader.read());
}

} // namespace charging::client
