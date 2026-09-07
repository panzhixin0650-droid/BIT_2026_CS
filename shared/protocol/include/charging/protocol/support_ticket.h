#pragma once

#include <QJsonObject>
#include <QString>
#include <optional>

namespace charging::protocol {

enum class TicketStatus { Open, InProgress, Resolved };
QString toString(TicketStatus status);
QString ticketStatusLabel(TicketStatus status);
bool parseTicketStatus(const QString &text, TicketStatus *status);
bool validTicketText(const QString &text, int maximum, bool required = true);
bool positiveTicketId(const QJsonValue &value, qint64 *id);

struct SupportTicketDraft {
    QString submissionId;
    QString title;
    QString summary;
    QString sourceModel;
};

struct SupportTicketDto : SupportTicketDraft {
    qint64 ticketId = 0;
    qint64 userId = 0;
    TicketStatus status = TicketStatus::Open;
    QString reply;
    QString createdAt;
    QString updatedAt;
};

QJsonObject toJson(const SupportTicketDraft &draft);
QJsonObject toJson(const SupportTicketDto &ticket);
bool fromJson(const QJsonObject &json, SupportTicketDraft *draft, QString *error = nullptr);
bool fromJson(const QJsonObject &json, SupportTicketDto *ticket, QString *error = nullptr);

}  // namespace charging::protocol
