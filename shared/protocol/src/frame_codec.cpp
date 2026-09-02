#include "charging/protocol/frame_codec.h"

#include "charging/protocol/protocol_constants.h"

#include <QJsonDocument>
#include <QJsonParseError>

namespace charging::protocol {
namespace {

QString frameErrorMessage(FrameError error)
{
    switch (error) {
    case FrameError::None:
        return {};
    case FrameError::InvalidLength:
        return QStringLiteral("frame body length must be in 1..262144 bytes");
    case FrameError::InvalidJson:
        return QStringLiteral("frame body is not valid JSON");
    case FrameError::RootNotObject:
        return QStringLiteral("JSON root must be an object");
    }
    return QStringLiteral("unknown frame error");
}

quint32 readBigEndianLength(const QByteArray &bytes)
{
    const auto *data = reinterpret_cast<const unsigned char *>(bytes.constData());
    return (static_cast<quint32>(data[0]) << 24U)
        | (static_cast<quint32>(data[1]) << 16U)
        | (static_cast<quint32>(data[2]) << 8U)
        | static_cast<quint32>(data[3]);
}

DecodeResult failureResult(FrameError error)
{
    return {{}, error, frameErrorMessage(error)};
}

}  // namespace

bool DecodeResult::ok() const noexcept
{
    return error == FrameError::None;
}

QByteArray encodeFrame(const QJsonObject &message)
{
    const QByteArray body = QJsonDocument(message).toJson(QJsonDocument::Compact);
    if (body.isEmpty() || body.size() > static_cast<qsizetype>(kMaxFrameBodyBytes)) {
        return {};
    }
    const quint32 length = static_cast<quint32>(body.size());

    QByteArray frame;
    frame.reserve(4 + body.size());
    frame.append(static_cast<char>((length >> 24U) & 0xFFU));
    frame.append(static_cast<char>((length >> 16U) & 0xFFU));
    frame.append(static_cast<char>((length >> 8U) & 0xFFU));
    frame.append(static_cast<char>(length & 0xFFU));
    frame.append(body);
    return frame;
}

DecodeResult FrameDecoder::append(const QByteArray &bytes)
{
    if (failure_ != FrameError::None) {
        return failureResult(failure_);
    }

    buffer_.append(bytes);
    DecodeResult result;

    while (buffer_.size() >= 4) {
        const quint32 bodyLength = readBigEndianLength(buffer_);
        if (bodyLength == 0 || bodyLength > kMaxFrameBodyBytes) {
            failure_ = FrameError::InvalidLength;
            buffer_.clear();
            return failureResult(failure_);
        }

        const qsizetype frameLength = 4 + static_cast<qsizetype>(bodyLength);
        if (buffer_.size() < frameLength) {
            break;
        }

        const QByteArray body = buffer_.mid(4, static_cast<qsizetype>(bodyLength));
        buffer_.remove(0, frameLength);

        QJsonParseError parseError;
        const QJsonDocument document = QJsonDocument::fromJson(body, &parseError);
        if (parseError.error != QJsonParseError::NoError || document.isNull()) {
            failure_ = FrameError::InvalidJson;
            buffer_.clear();
            return failureResult(failure_);
        }
        if (!document.isObject()) {
            failure_ = FrameError::RootNotObject;
            buffer_.clear();
            return failureResult(failure_);
        }

        result.messages.append(document.object());
    }

    return result;
}

void FrameDecoder::reset()
{
    buffer_.clear();
    failure_ = FrameError::None;
}

qsizetype FrameDecoder::bufferedBytes() const noexcept
{
    return buffer_.size();
}

bool FrameDecoder::failed() const noexcept
{
    return failure_ != FrameError::None;
}

}  // namespace charging::protocol
