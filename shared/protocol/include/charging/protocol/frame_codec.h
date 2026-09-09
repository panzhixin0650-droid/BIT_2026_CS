// 定义长度前缀 JSON 帧的编码与增量解码接口
#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QList>
#include <QString>

namespace charging::protocol {

// 分帧错误类型：长度非法、JSON 非法、根不是对象
enum class FrameError {
    None,
    InvalidLength,
    InvalidJson,
    RootNotObject,
};

// 一次解码的结果：解出的消息与错误信息
struct DecodeResult {
    QList<QJsonObject> messages;
    FrameError error = FrameError::None;
    QString errorMessage;

    [[nodiscard]] bool ok() const noexcept;
};

// 把一个 JSON 对象编码成带长度头的字节帧
[[nodiscard]] QByteArray encodeFrame(const QJsonObject &message);

// 逐批接收字节，处理一条消息被拆开或多条消息一起到达
class FrameDecoder {
public:
    [[nodiscard]] DecodeResult append(const QByteArray &bytes);
    void reset();

    [[nodiscard]] qsizetype bufferedBytes() const noexcept;
    [[nodiscard]] bool failed() const noexcept;

// 内部缓冲区与失败标记，失败后需要 reset
private:
    QByteArray buffer_;
    FrameError failure_ = FrameError::None;
};

}  // namespace charging::protocol
