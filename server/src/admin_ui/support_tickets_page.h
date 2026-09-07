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
public:
    explicit SupportTicketsPage(AdminFacade *facade, QWidget *parent = nullptr);
    void refresh(bool more = false);
    void clear();
private:
    void selectTicket();
    void save();
    AdminFacade *facade_;
    QListWidget *list_;
    QPlainTextEdit *summary_;
    QPlainTextEdit *reply_;
    QComboBox *status_;
    QLabel *notice_;
    QPushButton *save_;
    QPushButton *more_;
    QList<charging::protocol::SupportTicketDto> tickets_;
    bool hasMore_ = false;
};
}  // namespace charging::server
