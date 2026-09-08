#pragma once

#include "charging/protocol/support_ticket.h"
#include <QWidget>
#include <QList>

class QComboBox;
class QLabel;
class QListWidget;
class QPlainTextEdit;
class QPushButton;

namespace charging::server {
class AdminFacade;

class SupportTicketsPage final : public QWidget {
    Q_OBJECT
public:
    explicit SupportTicketsPage(AdminFacade *facade, QWidget *parent = nullptr);
    void refresh(bool more = false);
    void clear();
    qint64 selectedTicketId() const;
    void restoreTicketSelection(qint64 ticketId);
signals:
    void locatePileRequested(const QString &pileCode);
private:
    void selectTicket();
    void save();
    AdminFacade *facade_;
    QListWidget *list_;
    QPlainTextEdit *summary_;
    QPlainTextEdit *reply_;
    QComboBox *status_;
    QLabel *notice_;
    QLabel *count_;
    QPushButton *save_;
    QPushButton *more_;
    QPushButton *locatePile_;
    QList<charging::protocol::SupportTicketDto> tickets_;
    bool hasMore_ = false;
};
}  // namespace charging::server
