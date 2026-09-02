#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QList>
#include <QString>

namespace charging::protocol {

enum class FrameError {
    None,
    InvalidLength,
    InvalidJson,
    RootNotObject,
};

struct DecodeResult {
    QList<QJsonObject> messages;
    FrameError error = FrameError::None;
    QString errorMessage;

    [[nodiscard]] bool ok() const noexcept;
};

[[nodiscard]] QByteArray encodeFrame(const QJsonObject &message);

class FrameDecoder {
public:
    [[nodiscard]] DecodeResult append(const QByteArray &bytes);
    void reset();

    [[nodiscard]] qsizetype bufferedBytes() const noexcept;
    [[nodiscard]] bool failed() const noexcept;

private:
    QByteArray buffer_;
    FrameError failure_ = FrameError::None;
};

}  // namespace charging::protocol
