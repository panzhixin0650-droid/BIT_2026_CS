// 长度前缀加JSON体的帧编解码实现，供TCP通信复用
#include "charging/protocol/frame_codec.h"

#include "charging/protocol/protocol_constants.h"

#include <QJsonDocument>
#include <QJsonParseError>

namespace charging::protocol {
namespace {

// 把帧错误枚举转成可读英文提示，便于日志排查
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

// 读取前4字节大端长度，得到消息体字节数
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

// 把 JSON 写成紧凑格式，再加上 4 字节的长度头
QByteArray encodeFrame(const QJsonObject &message)
{
    const QByteArray body = QJsonDocument(message).toJson(QJsonDocument::Compact);
    // 空体或超出最大长度视为非法，返回空字节表示失败
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

// 把新收到的数据接在缓冲末尾，再逐条取出完整消息
DecodeResult FrameDecoder::append(const QByteArray &bytes)
{
    if (failure_ != FrameError::None) {
        return failureResult(failure_);
    }

    buffer_.append(bytes);
    DecodeResult result;

    while (buffer_.size() >= 4) {
        const quint32 bodyLength = readBigEndianLength(buffer_);
        // 长度非法则标记失败并丢弃缓冲，后续调用直接报错
        if (bodyLength == 0 || bodyLength > kMaxFrameBodyBytes) {
            failure_ = FrameError::InvalidLength;
            buffer_.clear();
            return failureResult(failure_);
        }

        const qsizetype frameLength = 4 + static_cast<qsizetype>(bodyLength);
        // 帧体还没收齐就先退出，等下次数据到达再解析
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

// 重连或复用解码器前清空缓冲与错误状态
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
