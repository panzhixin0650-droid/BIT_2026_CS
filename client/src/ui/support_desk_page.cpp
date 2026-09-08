#include "ui/support_desk_page.h"
#include "ui/busy_indicator.h"

#include <QHideEvent>
#include <QScrollArea>
#include <QTabBar>
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollBar>
#include <QShortcut>
#include <QTabWidget>
#include <QTextBrowser>
#include <QTextCursor>
#include <QUuid>
#include <QVBoxLayout>

namespace charging::client {
namespace {
QLabel *label(const QString &text, QWidget *parent, const char *name)
{
    auto *value = new QLabel(text, parent);
    value->setObjectName(QString::fromLatin1(name));
    value->setTextFormat(Qt::PlainText);
    value->setWordWrap(true);
    return value;
}
QString htmlText(const QString &text)
{
    return text.toHtmlEscaped().replace('\n', QStringLiteral("<br>"));
}
}

SupportDeskPage::SupportDeskPage(IChargingApi &api, AssistantService &desk,
                                     AssistantService &summarizer, QWidget *parent)
    : QWidget(parent), api_(api), desk_(desk), summarizer_(summarizer)
{
    setObjectName(QStringLiteral("supportDeskPage"));
    setWindowTitle(QStringLiteral("BIT CHARGE · 客服与工单"));
    setMinimumSize(0, 0);
    setStyleSheet(QStringLiteral(R"(
        QWidget#supportDeskPage { background: #f6f7f2; }
        QLabel { background: transparent; color: #304d42; }
        QLabel#deskHeading { font-size: 21px; font-weight: 700; }
        QLabel#deskDisclosure { color: #597668; font-size: 12px; }
        QTextBrowser, QPlainTextEdit, QLineEdit, QListWidget { background: white;
            color: #203d33; border: 1px solid #dce5d8; border-radius: 10px; padding: 9px; }
        QPushButton { background: #e8eee2; color: #31543f; border: none;
            border-radius: 12px; min-height:38px; padding:0 12px; font-size:12px; }
        QPushButton#deskSend, QPushButton#ticketSubmit { background: #245c45; color: white; }
        QPushButton:disabled, QPushButton#deskSend:disabled, QPushButton#ticketSubmit:disabled {
            color: #929c92; background: #edf0e9; }
        QTabWidget::pane { border: none; }
        QTabBar::tab { padding: 10px 8px; font-size:12px; background: transparent; color: #597668; }
        QTabBar::tab:selected { color: #245c45; border-bottom: 2px solid #245c45; }
    )"));
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(20, 20, 20, 20);
    root->setSpacing(10);
    auto *header = new QHBoxLayout;
    header->setSpacing(12);
    auto *back = new QPushButton(QStringLiteral("‹ 返回"), this);
    back->setObjectName("deskBackButton");
    back->setFlat(true);
    back->setCursor(Qt::PointingHandCursor);
    header->addWidget(back);
    header->addWidget(label(QStringLiteral("客服与工单"), this, "deskHeading"), 1);
    root->addLayout(header);
    connect(back, &QPushButton::clicked, this, &SupportDeskPage::backRequested);
    auto *backShortcut = new QShortcut(QKeySequence(Qt::Key_Escape), this);
    backShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(backShortcut, &QShortcut::activated, this, &SupportDeskPage::backRequested);
    root->addWidget(label(QStringLiteral("课程演示 · AI 模拟坐席  |  客服小悦 · 工号 008"), this, "deskDisclosure"));
    notice_ = label(QStringLiteral("仅发送你输入的内容和最近 4 轮对话；请勿提供密码、验证码或支付凭证。"), this, "deskNotice");
    root->addWidget(notice_);
    auto *waiting = new QHBoxLayout;
    busyIndicator_ = new BusyIndicator(this);
    busyIndicator_->setObjectName(QStringLiteral("deskBusySpinner"));
    busyStatus_ = label({}, this, "deskBusyStatus");
    cancelWaiting_ = new QPushButton(QStringLiteral("停止"), this);
    cancelWaiting_->setObjectName(QStringLiteral("deskCancelWaiting"));
    waiting->addWidget(busyIndicator_);
    waiting->addWidget(busyStatus_, 1);
    waiting->addWidget(cancelWaiting_);
    root->addLayout(waiting);
    tabs_ = new QTabWidget(this);
    tabs_->setObjectName(QStringLiteral("deskTabs"));
    tabs_->tabBar()->setExpanding(true);
    tabs_->setUsesScrollButtons(false);
    root->addWidget(tabs_, 1);

    auto *conversation = new QWidget(tabs_);
    auto *chatLayout = new QVBoxLayout(conversation);
    chatLayout->setContentsMargins(0, 8, 0, 0);
    chat_ = new QTextBrowser(conversation);
    chat_->setObjectName(QStringLiteral("deskChat"));
    chat_->setOpenLinks(false);
    chat_->setOpenExternalLinks(false);
    chatLayout->addWidget(chat_, 1);
    input_ = new QPlainTextEdit(conversation);
    input_->setObjectName(QStringLiteral("deskInput"));
    input_->setPlaceholderText(QStringLiteral("告诉小悦你遇到的问题（最多 1200 字，Ctrl+Enter 发送）"));
    input_->setFixedHeight(80);
    chatLayout->addWidget(input_);
    auto *actions = new QHBoxLayout;
    generate_ = new QPushButton(QStringLiteral("生成工单摘要"), conversation);
    generate_->setObjectName(QStringLiteral("ticketGenerate"));
    stop_ = new QPushButton(QStringLiteral("停止回复"), conversation);
    stop_->setObjectName(QStringLiteral("deskStop"));
    send_ = new QPushButton(QStringLiteral("发送"), conversation);
    send_->setObjectName(QStringLiteral("deskSend"));
    actions->addWidget(generate_);
    actions->addStretch();
    actions->addWidget(stop_);
    actions->addWidget(send_);
    chatLayout->addLayout(actions);
    tabs_->addTab(conversation, QStringLiteral("客服对话"));

    auto *draft = new QWidget(tabs_);
    auto *draftLayout = new QVBoxLayout(draft);
    draftLayout->setContentsMargins(0, 8, 0, 0);
    draftLayout->addWidget(label(QStringLiteral("核对后再提交"), draft, "ticketPreviewHeading"));
    draftLayout->addWidget(label(QStringLiteral("只上传下面的标题和摘要，不上传完整聊天记录。也可以直接手动填写。"), draft, "ticketPrivacy"));
    title_ = new QLineEdit(draft);
    title_->setObjectName(QStringLiteral("ticketTitle"));
    title_->setMaxLength(80);
    title_->setPlaceholderText(QStringLiteral("工单标题（1–80 字）"));
    draftLayout->addWidget(label(QStringLiteral("工单标题"), draft, "ticketTitleLabel"));
    draftLayout->addWidget(title_);
    repairFields_ = new QWidget(draft);
    auto *repairLayout = new QVBoxLayout(repairFields_);
    repairLayout->setContentsMargins(0, 0, 0, 0);
    repairPile_ = new QLineEdit(repairFields_);
    repairPile_->setObjectName(QStringLiteral("repairPileCode"));
    repairPile_->setMaxLength(64);
    repairPile_->setPlaceholderText(QStringLiteral("报修桩编号"));
    repairPile_->setAccessibleName(QStringLiteral("报修桩编号"));
    faultType_ = new QComboBox(repairFields_);
    faultType_->setObjectName(QStringLiteral("repairFaultType"));
    faultType_->setAccessibleName(QStringLiteral("故障类型"));
    faultType_->addItems({QStringLiteral("无法启动充电"), QStringLiteral("充电中断"),
                         QStringLiteral("充电枪或线缆损坏"), QStringLiteral("屏幕或扫码异常"),
                         QStringLiteral("其他故障")});
    repairLayout->addWidget(repairPile_, 1);
    repairLayout->addWidget(faultType_, 1);
    draftLayout->addWidget(repairFields_);
    summary_ = new QPlainTextEdit(draft);
    summary_->setObjectName(QStringLiteral("ticketSummary"));
    summary_->setPlaceholderText(QStringLiteral("问题现象、操作步骤、希望如何处理（1–4000 字）"));
    draftLayout->addWidget(label(QStringLiteral("问题摘要"), draft, "ticketSummaryLabel"));
    summary_->setMinimumHeight(140);
    draftLayout->addWidget(summary_, 1);
    draftNotice_ = label(QStringLiteral("AI 摘要可能有误，请核对；草稿尚未提交。"), draft, "ticketDraftNotice");
    draftLayout->addWidget(draftNotice_);
    auto *draftActions = new QHBoxLayout;
    newDraft_ = new QPushButton(QStringLiteral("新建草稿"), draft);
    newDraft_->setObjectName(QStringLiteral("ticketNewDraft"));
    submit_ = new QPushButton(QStringLiteral("确认提交"), draft);
    submit_->setObjectName(QStringLiteral("ticketSubmit"));
    draftActions->addWidget(newDraft_);
    draftActions->addStretch();
    draftActions->addWidget(submit_);
    draftLayout->addLayout(draftActions);
    auto *draftScroll = new QScrollArea(tabs_);
    draftScroll->setObjectName("ticketDraftScroll");
    draftScroll->setFrameShape(QFrame::NoFrame);
    draftScroll->setWidgetResizable(true);
    draftScroll->setWidget(draft);
    tabs_->addTab(draftScroll, QStringLiteral("工单草稿"));

    auto *tracking = new QWidget(tabs_);
    auto *trackingLayout = new QVBoxLayout(tracking);
    trackingLayout->setContentsMargins(0, 8, 0, 0);
    auto *trackingActions = new QHBoxLayout;
    refresh_ = new QPushButton(QStringLiteral("刷新我的工单"), tracking);
    refresh_->setObjectName(QStringLiteral("ticketRefresh"));
    more_ = new QPushButton(QStringLiteral("加载更多"), tracking);
    more_->setObjectName(QStringLiteral("ticketMore"));
    trackingActions->addWidget(refresh_);
    trackingActions->addStretch();
    trackingActions->addWidget(more_);
    trackingLayout->addLayout(trackingActions);
    tickets_ = new QListWidget(tracking);
    tickets_->setObjectName(QStringLiteral("myTickets"));
    trackingLayout->addWidget(tickets_, 1);
    ticketDetail_ = new QPlainTextEdit(tracking);
    ticketDetail_->setObjectName(QStringLiteral("myTicketDetail"));
    ticketDetail_->setReadOnly(true);
    ticketDetail_->setPlaceholderText(QStringLiteral("选择工单查看处理状态和管理员回复"));
    trackingLayout->addWidget(ticketDetail_, 1);
    tabs_->addTab(tracking, QStringLiteral("我的工单"));
    // Each entry owns its page state; the old three-way tab navigation is gone.
    tabs_->tabBar()->hide();
    generate_->hide();

    connect(send_, &QPushButton::clicked, this, &SupportDeskPage::send);
    auto *shortcut = new QShortcut(QKeySequence(QStringLiteral("Ctrl+Return")), input_);
    connect(shortcut, &QShortcut::activated, this, &SupportDeskPage::send);
    connect(stop_, &QPushButton::clicked, this, [this] { cancelModels(); updateControls(); });
    connect(cancelWaiting_, &QPushButton::clicked, this, [this] { cancelModels(); updateControls(); });
    waitingTimer_.setInterval(1000);
    connect(&waitingTimer_, &QTimer::timeout, this, &SupportDeskPage::updateControls);
    connect(generate_, &QPushButton::clicked, this, &SupportDeskPage::generateDraft);
    connect(submit_, &QPushButton::clicked, this, &SupportDeskPage::submitDraft);
    connect(newDraft_, &QPushButton::clicked, this, [this] {
        if (!createId_.isEmpty() || (draftLocked_ && !submitted_)) return;
        draftLocked_ = submitted_ = false;
        submissionId_.clear(); sourceModel_.clear();
        repairDraft_ = section_ == Section::Repair;
        repairPile_->clear();
        title_->clear(); summary_->clear();
        if (repairDraft_) title_->setText(QStringLiteral("充电桩故障报修"));
        draftNotice_->setText(QStringLiteral("新草稿尚未提交。"));
        updateControls();
    });
    for (auto *edit : {input_, summary_}) connect(edit, &QPlainTextEdit::textChanged, this, &SupportDeskPage::updateControls);
    connect(title_, &QLineEdit::textChanged, this, &SupportDeskPage::updateControls);
    connect(repairPile_, &QLineEdit::textChanged, this, &SupportDeskPage::updateControls);
    connect(refresh_, &QPushButton::clicked, this, [this] { refreshTickets(); });
    connect(more_, &QPushButton::clicked, this, [this] { refreshTickets(true); });
    connect(tickets_, &QListWidget::currentRowChanged, this, [this] { showTicket(); });
    connect(tabs_, &QTabWidget::currentChanged, this, [this](int index) {
        if (index == 2) refreshTickets();
        updateControls();
    });
    connect(&desk_, &AssistantService::answerUpdated, this, [this](quint64 id, const QString &text) {
        if (id != chatId_) return;
        updatePendingAnswer(text);
    });
    connect(&desk_, &AssistantService::finished, this, [this](quint64 id, const AssistantResult &result) {
        if (id != chatId_) return;
        chatId_ = 0;
        if (result.success) {
            transcript_.append({pendingQuestion_, result.answer});
            history_.append({pendingQuestion_, result.answer});
            while (history_.size() > 4) history_.removeFirst();
            notice_->setText(QStringLiteral("如需报修或查看处理进度，请前往“我的”。"));
        } else {
            input_->setPlainText(pendingQuestion_);
            notice_->setText(result.error + QStringLiteral(" 可重新发送，故障报修请前往“我的”。"));
        }
        pendingQuestion_.clear(); pendingAnswer_.clear();
        renderChat(); updateControls();
    });
    connect(&summarizer_, &AssistantService::finished, this, [this](quint64 id, const AssistantResult &result) {
        if (id != summaryId_) return;
        summaryId_ = 0;
        if (result.success && result.remote && protocol::validTicketText(result.answer, 4000)) {
            summary_->setPlainText(result.answer);
            title_->setText(QStringLiteral("充电服务问题反馈"));
            sourceModel_ = summarizer_.config().model;
            draftNotice_->setText(QStringLiteral("摘要已生成但尚未提交。请修改标题、核对事实，再点击确认提交。"));
        } else {
            draftNotice_->setText(result.error.isEmpty()
                ? QStringLiteral("摘要生成失败或超出长度，请手动填写或重试。")
                : result.error + QStringLiteral(" 原草稿保留，可手动填写。"));
        }
        updateControls();
    });
    connect(&api_, &IChargingApi::supportTicketCreated, this, [this](const TicketResult &result) {
        if (createId_.isEmpty() || createId_ != result.response.requestId) return;
        createId_.clear();
        if (result.ok() && result.payload) {
            submitted_ = true;
            draftNotice_->setText(QStringLiteral("工单 #%1 已提交，可在‘我的工单’查看处理结果。")
                                 .arg(result.payload->ticket.ticketId));
        } else {
            // Keep the same immutable submission ID/content for an uncertain retry.
            const auto message = errorMessage(result.response);
            if (result.response.code != protocol::ErrorCode::ServiceUnavailable
                && result.response.code != protocol::ErrorCode::InternalError) draftLocked_ = false;
            if (result.response.message == QStringLiteral("REPAIR_TICKETS_MIGRATION_REQUIRED")) draftLocked_ = false;
            draftNotice_->setText(message + QStringLiteral(" 重试沿用同一提交编号，不会重复建单。"));
        }
        updateControls();
    });
    connect(&api_, &IChargingApi::supportTicketsListed, this, [this](const TicketListResult &result) {
        if (listId_.isEmpty() || listId_ != result.response.requestId) return;
        listId_.clear();
        if (!result.ok() || !result.payload) {
            notice_->setText(errorMessage(result.response));
            updateControls(); return;
        }
        if (!listingMore_) { ticketRows_.clear(); tickets_->clear(); ticketDetail_->clear(); }
        for (const auto &ticket : result.payload->items) {
            emit ticketObserved(ticket);
            ticketRows_.append(ticket);
            tickets_->addItem(QStringLiteral("#%1  %2\n%3 · %4")
                .arg(ticket.ticketId).arg(ticket.title, protocol::ticketStatusLabel(ticket.status), ticket.createdAt));
            if (!ticket.pileCode.isEmpty()) tickets_->item(tickets_->count() - 1)->setText(
                QStringLiteral("[报修 · %1] ").arg(ticket.pileCode) + tickets_->item(tickets_->count() - 1)->text());
            if (ticket.submissionId == submissionId_ && draftLocked_) {
                submitted_ = true;
                draftNotice_->setText(QStringLiteral("已核实工单 #%1 提交成功。请勿重复提交。").arg(ticket.ticketId));
            }
        }
        hasMore_ = result.payload->hasMore;
        notice_->setText(ticketRows_.isEmpty() ? QStringLiteral("你还没有提交工单。")
            : QStringLiteral("已加载 %1 张工单；选择条目查看管理员回复。").arg(ticketRows_.size()));
        if (tickets_->currentRow() < 0 && tickets_->count()) tickets_->setCurrentRow(0);
        updateControls();
    });
    renderChat();
    updateControls();
}

void SupportDeskPage::openDesk(const QList<AssistantTurn> &history)
{
    section_ = Section::Conversation;
    tabs_->setCurrentIndex(0);
    if (!initialized_) {
        initialized_ = true;
        history_ = history.mid(qMax(qsizetype(0), history.size() - 4));
        if (!history_.isEmpty()) notice_->setText(QStringLiteral("已衔接助理最近 4 轮以内的对话。请勿发送密码或验证码。"));
    }
    updateControls();
    show();
}

void SupportDeskPage::send()
{
    const auto question = input_->toPlainText().trimmed();
    if (chatId_ || summaryId_ || question.isEmpty() || question.size() > 1200 || transcript_.size() >= 24) return;
    pendingQuestion_ = question;
    pendingAnswer_ = QStringLiteral("小悦正在回复…");
    chatId_ = desk_.ask(question, history_, true);
    if (!chatId_) { pendingQuestion_.clear(); pendingAnswer_.clear(); return; }
    input_->clear(); renderChat(); updateControls();
}

void SupportDeskPage::openRepair(const QString &pileCode)
{
    section_ = Section::Repair;
    tabs_->setCurrentIndex(1);
    updateControls();
    show();
    if (!submitted_ && (draftLocked_ || !title_->text().isEmpty() || !summary_->toPlainText().isEmpty())) {
        draftNotice_->setText(QStringLiteral("已保留未提交的报修单。可继续填写，或点击“新建报修”重新填写。"));
        return;
    }
    cancelModels();
    draftLocked_ = submitted_ = false;
    submissionId_.clear(); sourceModel_.clear();
    repairDraft_ = true;
    repairPile_->setText(pileCode);
    faultType_->setCurrentIndex(0);
    title_->setText(QStringLiteral("充电桩故障报修"));
    summary_->clear();
    summary_->setPlaceholderText(QStringLiteral("请描述故障现象、发生时间及已尝试的操作"));
    draftNotice_->setText(QStringLiteral("请核对桩编号、故障类型和描述。提交后可在‘我的工单’查看处理进度；报修不会自动结束充电。"));
    updateControls();
    (pileCode.isEmpty() ? static_cast<QWidget *>(repairPile_) : static_cast<QWidget *>(summary_))->setFocus();
}

void SupportDeskPage::openTickets()
{
    section_ = Section::Tickets;
    const bool wasTracking = tabs_->currentIndex() == 2;
    tabs_->setCurrentIndex(2);
    if (wasTracking) refreshTickets();
    updateControls();
    show();
}

void SupportDeskPage::refreshCurrentPage()
{
    if (section_ == Section::Tickets) refreshTickets();
}

void SupportDeskPage::confirmSubmission(const protocol::SupportTicketDto &ticket)
{
    if (!draftLocked_ || submissionId_.isEmpty() || ticket.submissionId != submissionId_) return;
    submitted_ = true;
    draftNotice_->setText(QStringLiteral("已核实工单 #%1 提交成功。请勿重复提交。").arg(ticket.ticketId));
    updateControls();
}

void SupportDeskPage::generateDraft()
{
    if (chatId_ || summaryId_ || history_.isEmpty() || draftLocked_ || repairDraft_) return;
    tabs_->setCurrentIndex(1);
    summaryId_ = summarizer_.ask(QStringLiteral("请根据最近对话整理一份待我核对的客服工单摘要。"), history_, true);
    draftNotice_->setText(QStringLiteral("正在整理摘要；生成完成后需要你确认，尚未提交到服务器。"));
    updateControls();
}

void SupportDeskPage::submitDraft()
{
    if (!createId_.isEmpty() || summaryId_ || submitted_) return;
    if (submissionId_.isEmpty()) submissionId_ = QUuid::createUuid().toString(QUuid::WithoutBraces);
    protocol::SupportTicketDraft draft{submissionId_, title_->text().trimmed(), summary_->toPlainText().trimmed(), sourceModel_};
    if (repairDraft_) {
        draft.pileCode = repairPile_->text().trimmed();
        draft.faultType = faultType_->currentText();
    }
    protocol::SupportTicketDraft validated;
    if (!protocol::fromJson(protocol::toJson(draft), &validated)) {
        draftNotice_->setText(repairDraft_
            ? QStringLiteral("请检查标题、描述和报修桩编号；桩编号仅支持字母、数字、短横线和下划线。")
            : QStringLiteral("请填写 1–80 字的标题和 1–4000 字的摘要，不含控制字符。")); return;
    }
    draftLocked_ = true;
    createId_ = api_.createSupportTicket(draft);
    draftNotice_->setText(QStringLiteral("正在提交工单，请稍候…"));
    updateControls();
}

void SupportDeskPage::refreshTickets(bool more)
{
    if (!listId_.isEmpty()) return;
    if (more && (!hasMore_ || ticketRows_.isEmpty())) return;
    listingMore_ = more;
    notice_->setText(more ? QStringLiteral("正在加载更多工单…") : QStringLiteral("正在加载我的工单…"));
    listId_ = api_.listSupportTickets(more ? std::optional<qint64>(ticketRows_.last().ticketId) : std::nullopt);
    updateControls();
}

void SupportDeskPage::cancelModels()
{
    const bool hadChat = chatId_ != 0;
    const bool hadSummary = summaryId_ != 0;
    chatId_ = summaryId_ = 0; // cancel() can finish synchronously.
    desk_.cancel(); summarizer_.cancel();
    if (hadChat) input_->setPlainText(pendingQuestion_);
    if (hadSummary) draftNotice_->setText(QStringLiteral("摘要生成已停止，原草稿保留。"));
    pendingQuestion_.clear(); pendingAnswer_.clear();
    renderChat();
}

void SupportDeskPage::hideEvent(QHideEvent *event)
{
    cancelModels(); updateControls();
    QWidget::hideEvent(event);
}

void SupportDeskPage::resetSession()
{
    createId_.clear(); listId_.clear();
    cancelModels();
    initialized_ = draftLocked_ = submitted_ = hasMore_ = false;
    repairDraft_ = false;
    repairPile_->clear();
    history_.clear(); transcript_.clear(); ticketRows_.clear();
    submissionId_.clear(); sourceModel_.clear();
    input_->clear(); title_->clear(); summary_->clear(); tickets_->clear(); ticketDetail_->clear();
    notice_->setText(QStringLiteral("仅发送你输入的内容和最近 4 轮对话；请勿提供密码、验证码或支付凭证。"));
    draftNotice_->setText(QStringLiteral("草稿尚未提交。"));
    tabs_->setCurrentIndex(0);
    renderChat(); updateControls(); hide();
}

void SupportDeskPage::renderChat()
{
    const bool bottom = chat_->verticalScrollBar()->value() >= chat_->verticalScrollBar()->maximum() - 24;
    const int previous = chat_->verticalScrollBar()->value();
    QString html = QStringLiteral("<p style='color:#52725f'>客服小悦 · 工号 008</p><p>您好，我是客服小悦。请问遇到了什么问题？故障报修和工单进度可在“我的”中办理。</p>");
    const auto add = [&html](const QString &question, const QString &answer) {
        html += QStringLiteral("<p align='right' style='color:#245c45'><b>你</b><br>%1</p>"
                               "<p style='background-color:#edf2e8;padding:12px'><b>客服小悦</b><br>%2</p>")
            .arg(htmlText(question), htmlText(answer));
    };
    for (const auto &turn : transcript_) add(turn.question, turn.answer);
    chat_->setHtml(html);
    if (!pendingQuestion_.isEmpty()) {
        QTextCursor cursor(chat_->document());
        cursor.movePosition(QTextCursor::End);
        cursor.insertBlock();
        cursor.insertHtml(QStringLiteral("<p align='right' style='color:#245c45'><b>你</b><br>%1</p>"
                                         "<p><b>客服小悦</b><br></p>").arg(htmlText(pendingQuestion_)));
        pendingAnswerPosition_ = cursor.position();
        QTextCharFormat body;
        body.setFontWeight(QFont::Normal);
        body.setForeground(QColor(QStringLiteral("#203d33")));
        cursor.insertText(pendingAnswer_, body);
    }
    chat_->verticalScrollBar()->setValue(bottom ? chat_->verticalScrollBar()->maximum() : previous);
}

void SupportDeskPage::updatePendingAnswer(const QString &text)
{
    const bool bottom = chat_->verticalScrollBar()->value() >= chat_->verticalScrollBar()->maximum() - 24;
    const int previous = chat_->verticalScrollBar()->value();
    QTextCursor cursor(chat_->document());
    cursor.movePosition(QTextCursor::End);
    const bool append = text.startsWith(pendingAnswer_);
    if (!append) {
        cursor.setPosition(pendingAnswerPosition_);
        cursor.movePosition(QTextCursor::End, QTextCursor::KeepAnchor);
    }
    QTextCharFormat body;
    body.setFontWeight(QFont::Normal);
    body.setForeground(QColor(QStringLiteral("#203d33")));
    cursor.insertText(append ? text.mid(pendingAnswer_.size()) : text, body);
    pendingAnswer_ = text;
    chat_->verticalScrollBar()->setValue(bottom ? chat_->verticalScrollBar()->maximum() : previous);
}

void SupportDeskPage::updateControls()
{
    for (int index = 0; index < tabs_->count(); ++index)
        tabs_->setTabEnabled(index, index == static_cast<int>(section_));
    const bool busy = chatId_ || summaryId_;
    busyIndicator_->setRunning(busy);
    busyStatus_->setVisible(busy);
    cancelWaiting_->setVisible(busy);
    if (busy && !waitingTimer_.isActive()) waitingTimer_.start();
    if (!busy) waitingTimer_.stop();
    if (busy) busyStatus_->setText(QStringLiteral("%1 · %2 秒 · 可停止")
        .arg(summaryId_ ? QStringLiteral("正在生成摘要") : QStringLiteral("小悦正在回复"))
        .arg(busyIndicator_->elapsedSeconds()));
    const auto text = input_->toPlainText().trimmed();
    send_->setEnabled(!busy && !text.isEmpty() && text.size() <= 1200 && transcript_.size() < 24);
    stop_->setEnabled(busy);
    generate_->setEnabled(!busy && !history_.isEmpty() && !draftLocked_ && !repairDraft_);
    repairFields_->setVisible(repairDraft_);
    findChild<QLabel *>(QStringLiteral("deskHeading"))->setText(section_ == Section::Repair
        ? QStringLiteral("故障报修") : section_ == Section::Tickets ? QStringLiteral("我的工单") : QStringLiteral("客服对话"));
    findChild<QLabel *>(QStringLiteral("deskDisclosure"))->setVisible(section_ == Section::Conversation);
    notice_->setVisible(section_ != Section::Repair);
    tabs_->setTabText(1, repairDraft_ ? QStringLiteral("报修单") : QStringLiteral("工单草稿"));
    findChild<QLabel *>(QStringLiteral("ticketPrivacy"))->setText(repairDraft_
        ? QStringLiteral("提交桩编号、故障类型、标题和描述，交由管理员跟进处理。")
        : QStringLiteral("只上传下面的标题和摘要，不上传完整聊天记录。也可以直接手动填写。"));
    repairPile_->setReadOnly(draftLocked_);
    faultType_->setEnabled(!draftLocked_);
    input_->setReadOnly(busy);
    title_->setReadOnly(draftLocked_ || summaryId_);
    summary_->setReadOnly(draftLocked_ || summaryId_);
    findChild<QLabel *>(QStringLiteral("ticketSummaryLabel"))->setText(
        QStringLiteral("%1 · %2/4000 字").arg(repairDraft_ ? QStringLiteral("故障描述") : QStringLiteral("问题摘要"))
            .arg(summary_->toPlainText().size()));
    submit_->setEnabled(!summaryId_ && createId_.isEmpty() && !submitted_
        && protocol::validTicketText(title_->text().trimmed(), 80)
        && (!repairDraft_ || !repairPile_->text().trimmed().isEmpty())
        && protocol::validTicketText(summary_->toPlainText().trimmed(), 4000));
    submit_->setText(submitted_ ? QStringLiteral("已提交")
        : draftLocked_ ? QStringLiteral("重试同一工单") : QStringLiteral("确认提交"));
    newDraft_->setEnabled(!summaryId_ && createId_.isEmpty() && (!draftLocked_ || submitted_));
    newDraft_->setText(section_ == Section::Repair ? QStringLiteral("新建报修") : QStringLiteral("新建草稿"));
    refresh_->setEnabled(listId_.isEmpty());
    more_->setEnabled(listId_.isEmpty() && hasMore_);
    if (transcript_.size() >= 24) notice_->setText(QStringLiteral("本次客服对话已达 24 轮。如需报修，请前往“我的”。"));
}

void SupportDeskPage::showTicket()
{
    const int row = tickets_->currentRow();
    if (row < 0 || row >= ticketRows_.size()) { ticketDetail_->clear(); return; }
    const auto &ticket = ticketRows_[row];
    ticketDetail_->setPlainText(QStringLiteral("工单 #%1 · %2\n%3\n\n%4\n\n管理员回复：\n%5\n\n更新时间：%6")
        .arg(ticket.ticketId).arg(protocol::ticketStatusLabel(ticket.status), ticket.title, ticket.summary,
             ticket.reply.isEmpty() ? QStringLiteral("暂无回复") : ticket.reply, ticket.updatedAt));
    if (!ticket.pileCode.isEmpty()) ticketDetail_->appendPlainText(
        QStringLiteral("\n充电桩报修 · %1\n故障类型：%2").arg(ticket.pileCode, ticket.faultType));
}

QString SupportDeskPage::errorMessage(const ApiResponse &response)
{
    if (response.message == QStringLiteral("REPAIR_TICKETS_MIGRATION_REQUIRED"))
        return QStringLiteral("服务端尚未启用报修，请联系管理员升级报修功能。");
    if (response.code == protocol::ErrorCode::NotFound)
        return QStringLiteral("找不到该充电桩，请核对桩身编号后重试。");
    if (response.code == protocol::ErrorCode::InvalidSession) {
        const QString message = QStringLiteral("登录状态已失效，请重新登录。");
        emit invalidSession(message);
        return message;
    }
    if (response.message == QStringLiteral("SUPPORT_TICKETS_MIGRATION_REQUIRED"))
        return QStringLiteral("服务端尚未启用工单，请管理员执行 002 工单迁移；其他业务不受影响。");
    if (response.code == protocol::ErrorCode::Forbidden) return QStringLiteral("当前账户无权使用工单功能。");
    if (response.code == protocol::ErrorCode::InvalidRequest)
        return QStringLiteral("工单内容或提交编号不匹配，请检查输入，并确认服务端已升级支持工单。");
    return QStringLiteral("暂时无法确认工单结果。请恢复连接后刷新‘我的工单’核对，或重试同一工单。");
}

}  // namespace charging::client
