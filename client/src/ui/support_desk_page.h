#pragma once

#include "api/i_charging_api.h"
#include "assistant/assistant_service.h"
#include <QWidget>

class QLabel;
class QComboBox;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QListWidget;
class QTabWidget;
class QTextBrowser;

namespace charging::client {

class BusyIndicator;

// Isolated UI orchestration: models only produce text; only confirmed drafts reach IChargingApi.
class SupportDeskPage final : public QWidget {
    Q_OBJECT
public:
    SupportDeskPage(IChargingApi &api, AssistantService &desk, AssistantService &summarizer,
                      QWidget *parent = nullptr);
    void openDesk(const QList<AssistantTurn> &history = {});
    void openRepair(const QString &pileCode);
    void resetSession();
signals:
    void backRequested();
    void invalidSession(const QString &message);
protected:
    void hideEvent(QHideEvent *event) override;
private:
    void send();
    void generateDraft();
    void submitDraft();
    void refreshTickets(bool more = false);
    void cancelModels();
    void renderChat();
    void updatePendingAnswer(const QString &text);
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
    BusyIndicator *busyIndicator_;
    QLabel *busyStatus_;
    QPushButton *cancelWaiting_;
    QTimer waitingTimer_;
    QPushButton *send_;
    QPushButton *stop_;
    QPushButton *generate_;
    QLineEdit *title_;
    QWidget *repairFields_;
    QLineEdit *repairPile_;
    QComboBox *faultType_;
    bool repairDraft_ = false;
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
    int pendingAnswerPosition_ = 0;
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
