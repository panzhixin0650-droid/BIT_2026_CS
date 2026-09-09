// 定义客服工单的数据结构、校验与 JSON 互转声明
#pragma once

#include <QJsonObject>
#include <QString>
#include <optional>

namespace charging::protocol {

// 工单状态及其协议字符串、中文标签与反向解析
enum class TicketStatus { Open, InProgress, Resolved };
QString toString(TicketStatus status);
QString ticketStatusLabel(TicketStatus status);
bool parseTicketStatus(const QString &text, TicketStatus *status);
// 通用校验：文本长度与字符合法性、正整数工单 ID
bool validTicketText(const QString &text, int maximum, bool required = true);
bool positiveTicketId(const QJsonValue &value, qint64 *id);

// 工单草稿，由用户确认后再提交给服务端
struct SupportTicketDraft {
    QString submissionId;
    QString title;
    QString summary;
    QString sourceModel;
    // Both empty for ordinary support; otherwise a device repair ticket.
    // 报修专用字段：桩编号与故障类型
    QString pileCode;
    QString faultType;
};

// 服务端返回的完整工单，附带 ID、状态、回复与时间
struct SupportTicketDto : SupportTicketDraft {
    qint64 ticketId = 0;
    qint64 userId = 0;
    TicketStatus status = TicketStatus::Open;
    QString reply;
    QString createdAt;
    QString updatedAt;
};

// 草稿与完整工单的序列化及解析入口
QJsonObject toJson(const SupportTicketDraft &draft);
QJsonObject toJson(const SupportTicketDto &ticket);
bool fromJson(const QJsonObject &json, SupportTicketDraft *draft, QString *error = nullptr);
bool fromJson(const QJsonObject &json, SupportTicketDto *ticket, QString *error = nullptr);

}  // namespace charging::protocol
