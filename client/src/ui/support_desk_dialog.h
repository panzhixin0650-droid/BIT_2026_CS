#pragma once

#include "api/i_charging_api.h"
#include "assistant/assistant_service.h"
#include <QDialog>

class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QListWidget;
class QTabWidget;
class QTextBrowser;

namespace charging::client {

// Isolated UI orchestration: models only produce text; only confirmed drafts reach IChargingApi.
class SupportDeskDialog final : public QDialog {
    Q_OBJECT
public:
    SupportDeskDialog(IChargingApi &api, AssistantService &desk, AssistantService &summarizer,
                      QWidget *parent = nullptr);
    void openDesk(const QList<AssistantTurn> &history = {});
    void resetSession();
signals:
    void invalidSession(const QString &message);
protected:
    void closeEvent(QCloseEvent *event) override;
private:
    void send();
    void generateDraft();
    void submitDraft();
    void refreshTickets(bool more = false);
    void cancelModels();
    void renderChat();
    void updateControls();
    void showTicket();
    QString errorMessage(const ApiResponse &response);

    IChargingApi &api_;
    AssistantService &desk_;
    AssistantService &summarizer_;
    QTabWidget *tabs_;
    QTextBrowser *chat_;
    QPlainTextEdit *input_;
    QLabel *notice_;
    QPushButton *send_;
    QPushButton *stop_;
    QPushButton *generate_;
    QLineEdit *title_;
    QPlainTextEdit *summary_;
    QLabel *draftNotice_;
    QPushButton *submit_;
    QPushButton *newDraft_;
    QListWidget *tickets_;
    QPlainTextEdit *ticketDetail_;
    QPushButton *refresh_;
    QPushButton *more_;
    QList<AssistantTurn> history_;
    QList<AssistantTurn> transcript_;
    QList<protocol::SupportTicketDto> ticketRows_;
    QString pendingQuestion_;
    QString pendingAnswer_;
    quint64 chatId_ = 0;
    quint64 summaryId_ = 0;
    QString createId_;
    QString listId_;
    QString submissionId_;
    QString sourceModel_;
    bool initialized_ = false;
    bool draftLocked_ = false;
    bool submitted_ = false;
    bool listingMore_ = false;
    bool hasMore_ = false;
};

}  // namespace charging::client
