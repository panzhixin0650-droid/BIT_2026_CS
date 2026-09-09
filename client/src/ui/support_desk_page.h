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
// 本文件声明客服工单页：模型只出文本，确认后才提交工单
class SupportDeskPage final : public QWidget {
    Q_OBJECT
public:
    SupportDeskPage(IChargingApi &api, AssistantService &desk, AssistantService &summarizer,
                      QWidget *parent = nullptr);
    // 三个入口分别打开对话、故障报修与我的工单
    void openDesk(const QList<AssistantTurn> &history = {});
    void openRepair(const QString &pileCode);
    void openTickets();
    void refreshCurrentPage();
    void confirmSubmission(const protocol::SupportTicketDto &ticket);
    void resetSession();
signals:
    void backRequested();
    void invalidSession(const QString &message);
    void ticketObserved(const protocol::SupportTicketDto &ticket);
// 页面隐藏时取消进行中的模型请求
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

    // 以下保存控件、对话历史与提交状态
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
    enum class Section { Conversation, Repair, Tickets };
    Section section_ = Section::Conversation;
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
    // 提交编号用于重试时不重复建单
    QString submissionId_;
    QString sourceModel_;
    bool initialized_ = false;
    bool draftLocked_ = false;
    bool submitted_ = false;
    bool listingMore_ = false;
    bool hasMore_ = false;
};

}  // namespace charging::client
