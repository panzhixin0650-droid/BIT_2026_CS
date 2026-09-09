// 实现工单状态转换、字段校验与 JSON 互转
#include "charging/protocol/support_ticket.h"

#include <QDateTime>
#include <QRegularExpression>
#include <cmath>

namespace charging::protocol {

// 状态转协议字符串
QString toString(TicketStatus status)
{
    switch (status) {
    case TicketStatus::Open: return QStringLiteral("OPEN");
    case TicketStatus::InProgress: return QStringLiteral("IN_PROGRESS");
    case TicketStatus::Resolved: return QStringLiteral("RESOLVED");
    }
    return {};
}

// 状态对应的界面中文标签
QString ticketStatusLabel(TicketStatus status)
{
    switch (status) {
    case TicketStatus::Open: return QStringLiteral("待处理");
    case TicketStatus::InProgress: return QStringLiteral("处理中");
    case TicketStatus::Resolved: return QStringLiteral("已解决");
    }
    return {};
}

// 按协议字符串反查状态枚举
bool parseTicketStatus(const QString &text, TicketStatus *status)
{
    for (auto value : {TicketStatus::Open, TicketStatus::InProgress, TicketStatus::Resolved}) {
        if (text == toString(value)) { *status = value; return true; }
    }
    return false;
}

// 校验文本长度、控制字符以及代理对是否成对
bool validTicketText(const QString &text, int maximum, bool required)
{
    if (text.size() > maximum || (required && text.trimmed().isEmpty())) return false;
    for (qsizetype i = 0; i < text.size(); ++i) {
        const QChar c = text.at(i);
        if (c.category() == QChar::Other_Control && c != '\n' && c != '\r' && c != '\t')
            return false;
        if (c.isHighSurrogate()) {
            if (++i >= text.size() || !text.at(i).isLowSurrogate()) return false;
        } else if (c.isLowSurrogate()) return false;
    }
    return true;
}

// 工单 ID 必须是安全范围内的正整数
bool positiveTicketId(const QJsonValue &value, qint64 *id)
{
    const double number = value.toDouble();
    if (!value.isDouble() || !std::isfinite(number) || number < 1
        || number > 9007199254740991.0 || std::floor(number) != number) return false;
    *id = static_cast<qint64>(number);
    return true;
}

// 草稿转 JSON，含报修信息时才附加 repair 对象
QJsonObject toJson(const SupportTicketDraft &draft)
{
    QJsonObject json{{"submissionId", draft.submissionId}, {"title", draft.title},
            {"summary", draft.summary}, {"sourceModel", draft.sourceModel}};
    if (!draft.pileCode.isEmpty() || !draft.faultType.isEmpty()) {
        json.insert("repair", QJsonObject{{"pileCode", draft.pileCode}, {"faultType", draft.faultType}});
    }
    return json;
}

// 完整工单在草稿基础上补 ID、状态、回复与时间
QJsonObject toJson(const SupportTicketDto &ticket)
{
    auto json = toJson(static_cast<const SupportTicketDraft &>(ticket));
    json.insert("ticketId", ticket.ticketId);
    json.insert("userId", ticket.userId);
    json.insert("status", toString(ticket.status));
    json.insert("reply", ticket.reply);
    json.insert("createdAt", ticket.createdAt);
    json.insert("updatedAt", ticket.updatedAt);
    return json;
}

// 解析草稿：字段数量、UUID、长度与模型名格式全部校验
bool fromJson(const QJsonObject &json, SupportTicketDraft *draft, QString *error)
{
    static const QRegularExpression uuid(QStringLiteral(
        "^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}\\z"));
    static const QRegularExpression model(QStringLiteral("^[A-Za-z0-9._:-]{0,120}\\z"));
    const QStringList keys{"submissionId", "title", "summary", "sourceModel"};
    bool valid = draft != nullptr && json.size() == keys.size() + (json.contains("repair") ? 1 : 0);
    for (const auto &key : keys) valid = valid && json.value(key).isString();
    SupportTicketDraft parsed{json.value("submissionId").toString(), json.value("title").toString(),
                              json.value("summary").toString(), json.value("sourceModel").toString()};
    valid = valid && uuid.match(parsed.submissionId).hasMatch()
        && validTicketText(parsed.title, 80) && validTicketText(parsed.summary, 4000)
        && model.match(parsed.sourceModel).hasMatch();
    // 报修子对象要求桩编号与故障类型都合法
    if (json.contains("repair")) {
        const auto repair = json.value("repair").toObject();
        parsed.pileCode = repair.value("pileCode").toString();
        parsed.faultType = repair.value("faultType").toString();
        static const QRegularExpression code(QStringLiteral("^[A-Za-z0-9_-]{1,64}\\z"));
        valid = valid && json.value("repair").isObject() && repair.size() == 2
            && repair.value("pileCode").isString() && repair.value("faultType").isString()
            && code.match(parsed.pileCode).hasMatch()
            && validTicketText(parsed.faultType, 64)
            && parsed.faultType == parsed.faultType.trimmed();
    }
    if (!valid) { if (error) *error = QStringLiteral("Invalid support ticket draft"); return false; }
    *draft = parsed;
    if (error) error->clear();
    return true;
}

// 解析服务端返回的完整工单
bool fromJson(const QJsonObject &json, SupportTicketDto *ticket, QString *error)
{
    SupportTicketDto parsed;
    QJsonObject draft;
    for (const QString &key : {QStringLiteral("submissionId"), QStringLiteral("title"),
                              QStringLiteral("summary"), QStringLiteral("sourceModel")})
        draft.insert(key, json.value(key));
    if (json.contains("repair")) draft.insert("repair", json.value("repair"));
    // 时间需为以 Z 结尾的 UTC ISO 字符串
    const auto utc = [](const QJsonValue &value) {
        return value.isString() && value.toString().endsWith('Z')
            && QDateTime::fromString(value.toString(), Qt::ISODate).isValid();
    };
    if (!ticket || !fromJson(draft, static_cast<SupportTicketDraft *>(&parsed))
        || !positiveTicketId(json.value("ticketId"), &parsed.ticketId)
        || !positiveTicketId(json.value("userId"), &parsed.userId)
        || !parseTicketStatus(json.value("status").toString(), &parsed.status)
        || !json.value("reply").isString()
        // 已解决的工单必须带有回复内容
        || !validTicketText(json.value("reply").toString(), 2000, parsed.status == TicketStatus::Resolved)
        || !utc(json.value("createdAt")) || !utc(json.value("updatedAt"))) {
        if (error) *error = QStringLiteral("Invalid support ticket response");
        return false;
    }
    parsed.reply = json.value("reply").toString();
    parsed.createdAt = json.value("createdAt").toString();
    parsed.updatedAt = json.value("updatedAt").toString();
    *ticket = parsed;
    if (error) error->clear();
    return true;
}

}  // namespace charging::protocol
