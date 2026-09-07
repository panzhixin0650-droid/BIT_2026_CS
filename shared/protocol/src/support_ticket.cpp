#include "charging/protocol/support_ticket.h"

#include <QDateTime>
#include <QRegularExpression>
#include <cmath>

namespace charging::protocol {

QString toString(TicketStatus status)
{
    switch (status) {
    case TicketStatus::Open: return QStringLiteral("OPEN");
    case TicketStatus::InProgress: return QStringLiteral("IN_PROGRESS");
    case TicketStatus::Resolved: return QStringLiteral("RESOLVED");
    }
    return {};
}

QString ticketStatusLabel(TicketStatus status)
{
    switch (status) {
    case TicketStatus::Open: return QStringLiteral("待处理");
    case TicketStatus::InProgress: return QStringLiteral("处理中");
    case TicketStatus::Resolved: return QStringLiteral("已解决");
    }
    return {};
}

bool parseTicketStatus(const QString &text, TicketStatus *status)
{
    for (auto value : {TicketStatus::Open, TicketStatus::InProgress, TicketStatus::Resolved}) {
        if (text == toString(value)) { *status = value; return true; }
    }
    return false;
}

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

bool positiveTicketId(const QJsonValue &value, qint64 *id)
{
    const double number = value.toDouble();
    if (!value.isDouble() || !std::isfinite(number) || number < 1
        || number > 9007199254740991.0 || std::floor(number) != number) return false;
    *id = static_cast<qint64>(number);
    return true;
}

QJsonObject toJson(const SupportTicketDraft &draft)
{
    return {{"submissionId", draft.submissionId}, {"title", draft.title},
            {"summary", draft.summary}, {"sourceModel", draft.sourceModel}};
}

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

bool fromJson(const QJsonObject &json, SupportTicketDraft *draft, QString *error)
{
    static const QRegularExpression uuid(QStringLiteral(
        "^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}\\z"));
    static const QRegularExpression model(QStringLiteral("^[A-Za-z0-9._:-]{0,120}\\z"));
    const QStringList keys{"submissionId", "title", "summary", "sourceModel"};
    bool valid = draft != nullptr && json.size() == keys.size();
    for (const auto &key : keys) valid = valid && json.value(key).isString();
    SupportTicketDraft parsed{json.value("submissionId").toString(), json.value("title").toString(),
                              json.value("summary").toString(), json.value("sourceModel").toString()};
    valid = valid && uuid.match(parsed.submissionId).hasMatch()
        && validTicketText(parsed.title, 80) && validTicketText(parsed.summary, 4000)
        && model.match(parsed.sourceModel).hasMatch();
    if (!valid) { if (error) *error = QStringLiteral("Invalid support ticket draft"); return false; }
    *draft = parsed;
    if (error) error->clear();
    return true;
}

bool fromJson(const QJsonObject &json, SupportTicketDto *ticket, QString *error)
{
    SupportTicketDto parsed;
    QJsonObject draft;
    for (const QString &key : {QStringLiteral("submissionId"), QStringLiteral("title"),
                              QStringLiteral("summary"), QStringLiteral("sourceModel")})
        draft.insert(key, json.value(key));
    const auto utc = [](const QJsonValue &value) {
        return value.isString() && value.toString().endsWith('Z')
            && QDateTime::fromString(value.toString(), Qt::ISODate).isValid();
    };
    if (!ticket || !fromJson(draft, static_cast<SupportTicketDraft *>(&parsed))
        || !positiveTicketId(json.value("ticketId"), &parsed.ticketId)
        || !positiveTicketId(json.value("userId"), &parsed.userId)
        || !parseTicketStatus(json.value("status").toString(), &parsed.status)
        || !json.value("reply").isString()
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
