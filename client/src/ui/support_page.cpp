#include "ui/support_page.h"

#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QFrame>
#include <QGridLayout>
#include <QInputMethodEvent>
#include <QKeyEvent>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <functional>

namespace charging::client {
namespace {

class ChatInput final : public QPlainTextEdit {
public:
    explicit ChatInput(QWidget *parent) : QPlainTextEdit(parent) {}
    std::function<void()> sendRequested;
protected:
    void inputMethodEvent(QInputMethodEvent *event) override
    {
        composing_ = !event->preeditString().isEmpty();
        QPlainTextEdit::inputMethodEvent(event);
    }
    void keyPressEvent(QKeyEvent *event) override
    {
        if ((event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter)
            && !(event->modifiers() & Qt::ShiftModifier) && !composing_) {
            if (sendRequested) { sendRequested(); }
            event->accept();
            return;
        }
        QPlainTextEdit::keyPressEvent(event);
    }
private:
    bool composing_ = false;
};

QLabel *textLabel(const QString &text, QWidget *parent, const QString &name = {})
{
    auto *label = new QLabel(text, parent);
    label->setObjectName(name);
    label->setTextFormat(Qt::PlainText);
    label->setWordWrap(true);
    label->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    return label;
}

}  // namespace

SupportPage::SupportPage(AssistantService &service, QWidget *parent)
    : QWidget(parent), service_(service)
{
    setObjectName(QStringLiteral("supportPage"));
    setStyleSheet(QStringLiteral(R"(
        QWidget#supportPage, QWidget#assistantCanvas { background: #f6f7f2; }
        QLabel { background: transparent; }
        QLabel#assistantLogo { background: #245c45; color: white; border-radius: 12px;
            font-size: 16px; font-weight: 700; }
        QLabel#supportHeading { font-size: 19px; font-weight: 700; color: #203d33; }
        QLabel#assistantBrand { font-size: 10px; color: #7e8d77; letter-spacing: 2px; }
        QLabel#assistantStatus, QLabel#assistantPrivacy, QLabel#assistantCounter {
            color: #697969; font-size: 11px; }
        QFrame#supportCard { background: white; border: 1px solid #e1e7dc; border-radius: 18px; }
        QLabel#supportTitle { color: #203d33; font-size: 23px; font-weight: 700; }
        QLabel#assistantWelcomeTag { color: #51743d; font-size: 12px; font-weight: 600; }
        QLabel#assistantWelcomeDescription { color: #697969; font-size: 13px; }
        QPushButton#assistantNewChat { background: transparent; color: #446a49;
            border: 1px solid #dce3d5; border-radius: 10px; min-height: 30px;
            padding: 0 10px; font-size: 12px; }
        QPushButton[assistantSuggestion="true"] { background: #f4f6ef;
            color: #425c43; border: 1px solid #e1e7dc; border-radius: 12px;
            min-height: 60px; padding: 5px; font-size: 13px; font-weight: 500; }
        QPushButton[assistantSuggestion="true"]:hover { background: #eaf1df; border-color: #a6bf91; }
        QFrame[chatRole="user"] { background: #e9f1dd; border: 1px solid #d4e3c2; border-radius: 15px; }
        QFrame[chatRole="assistant"] { background: white; border: 1px solid #e1e7dc; border-radius: 15px; }
        QLabel[chatBody="true"] { color: #2d4736; font-size: 14px; }
        QLabel[chatCaption="true"] { color: #74836a; font-size: 11px; font-weight: 600; }
        QLabel#assistantError { color: #b55b3b; font-size: 12px; }
        QLabel#assistantSourceText { color: #74836a; font-size: 11px; }
        QToolButton { color: #627c52; background: transparent; border: none; padding: 4px 0;
            font-size: 11px; text-align: left; }
        QFrame#assistantComposer { background: white; border: 1px solid #cbd9bd; border-radius: 15px; }
        QPlainTextEdit#assistantInput { background: transparent; color: #2d4736; border: none;
            padding: 4px; font-size: 14px; selection-background-color: #d7e7c4; }
        QPushButton#assistantSend { background: #245c45; color: white; border: none;
            min-height: 32px; border-radius: 10px; font-size: 13px; }
        QPushButton#assistantSend:disabled { background: #e2e9d9; color: #95a28a; }
        QPushButton#assistantStop { background: #edf3e4; color: #48673e; border: none;
            min-height: 32px; border-radius: 10px; font-size: 12px; }
        QComboBox#assistantMode { min-height: 28px; padding: 0 10px; border-radius: 9px; font-size: 12px; }
        QScrollBar:vertical { background: transparent; width: 6px; margin: 0; }
        QScrollBar::handle:vertical { background: #cbd5c2; border-radius: 3px; min-height: 30px; }
        QScrollBar::handle:vertical:hover { background: #9aaf8e; }
        QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; border: none; }
        QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: transparent; }
    )"));
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(16, 16, 16, 8);
    layout->setSpacing(10);

    auto *header = new QHBoxLayout;
    auto *logo = new QLabel(QStringLiteral("AI"), this);
    logo->setObjectName(QStringLiteral("assistantLogo"));
    logo->setAlignment(Qt::AlignCenter);
    logo->setFixedSize(40, 40);
    auto *heading = new QVBoxLayout;
    heading->setSpacing(0);
    heading->addWidget(textLabel(QStringLiteral("充电小助手"), this, QStringLiteral("supportHeading")));
    heading->addWidget(textLabel(QStringLiteral("BIT CHARGE · ASSISTANT"), this, QStringLiteral("assistantBrand")));
    auto *newChat = new QPushButton(QStringLiteral("＋ 新对话"), this);
    newChat->setObjectName(QStringLiteral("assistantNewChat"));
    header->addWidget(logo);
    header->addLayout(heading, 1);
    header->addWidget(newChat);
    layout->addLayout(header);

    auto *modeRow = new QHBoxLayout;
    mode_ = new QComboBox(this);
    mode_->setObjectName(QStringLiteral("assistantMode"));
    mode_->addItems({QStringLiteral("AI + 知识库"), QStringLiteral("仅本地知识库")});
    mode_->setCurrentIndex(service_.config().isReady() ? 0 : 1);
    status_ = textLabel({}, this, QStringLiteral("assistantStatus"));
    modeRow->addWidget(mode_);
    modeRow->addWidget(status_, 1);
    layout->addLayout(modeRow);

    scroll_ = new QScrollArea(this);
    scroll_->setObjectName(QStringLiteral("assistantScroll"));
    scroll_->setFrameShape(QFrame::NoFrame);
    scroll_->setWidgetResizable(true);
    scroll_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    canvas_ = new QWidget(scroll_);
    canvas_->setObjectName(QStringLiteral("assistantCanvas"));
    messagesLayout_ = new QVBoxLayout(canvas_);
    messagesLayout_->setContentsMargins(0, 4, 4, 10);
    messagesLayout_->setSpacing(14);
    auto *card = new QFrame(canvas_);
    welcome_ = card;
    card->setObjectName(QStringLiteral("supportCard"));
    auto *cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(18, 24, 18, 20);
    cardLayout->setSpacing(14);
    cardLayout->addWidget(textLabel(QStringLiteral("你的充电向导，随时在这里"), card,
                                   QStringLiteral("assistantWelcomeTag")));
    cardLayout->addWidget(textLabel(QStringLiteral("你好，有什么\n可以帮你？"), card,
                                   QStringLiteral("supportTitle")));
    cardLayout->addWidget(textLabel(QStringLiteral("从找站到结束充电，让每一步更清楚。\n选一个问题，或在下方直接问我。"),
                                   card, QStringLiteral("assistantWelcomeDescription")));
    auto *grid = new QGridLayout;
    grid->setSpacing(10);
    const QStringList captions{QStringLiteral("附近充电站\n怎么查找  ↗"),
        QStringLiteral("预约充电\n如何操作  ↗"), QStringLiteral("充电费用\n怎么计算  ↗"),
        QStringLiteral("余额不足\n如何处理  ↗"), QStringLiteral("路线导航\n带我去充电  ↗"),
        QStringLiteral("电桩故障\n怎么办  ↗")};
    const auto questions = service_.knowledgeBase().suggestedQuestions();
    for (int i = 0; i < questions.size(); ++i) {
        auto *button = new QPushButton(captions.value(i, questions[i]), card);
        button->setObjectName(QStringLiteral("assistantSuggestion%1").arg(i));
        button->setProperty("assistantSuggestion", true);
        button->setProperty("question", questions[i]);
        button->setToolTip(questions[i]);
        button->setAccessibleName(questions[i]);
        button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        connect(button, &QPushButton::clicked, this, [this, question = questions[i]]() { submit(question); });
        suggestions_.append(button);
        grid->addWidget(button, i / 2, i % 2);
    }
    cardLayout->addLayout(grid);
    messagesLayout_->addWidget(card);
    messagesLayout_->addStretch(1);
    scroll_->setWidget(canvas_);
    layout->addWidget(scroll_, 1);

    auto *composer = new QFrame(this);
    composer->setObjectName(QStringLiteral("assistantComposer"));
    auto *composerLayout = new QVBoxLayout(composer);
    composerLayout->setContentsMargins(10, 6, 10, 8);
    composerLayout->setSpacing(3);
    auto *editor = new ChatInput(composer);
    input_ = editor;
    input_->setObjectName(QStringLiteral("assistantInput"));
    input_->setPlaceholderText(QStringLiteral("问问充电、预约、导航…"));
    input_->setFixedHeight(58);
    input_->setTabChangesFocus(true);
    input_->setAccessibleName(QStringLiteral("向充电助理提问"));
    editor->sendRequested = [this]() { submit(input_->toPlainText()); };
    composerLayout->addWidget(input_);
    auto *actions = new QHBoxLayout;
    counter_ = textLabel({}, composer, QStringLiteral("assistantCounter"));
    send_ = new QPushButton(QStringLiteral("发送 ↑"), composer);
    send_->setObjectName(QStringLiteral("assistantSend"));
    stop_ = new QPushButton(QStringLiteral("■ 停止"), composer);
    stop_->setObjectName(QStringLiteral("assistantStop"));
    actions->addWidget(counter_, 1);
    actions->addWidget(stop_);
    actions->addWidget(send_);
    composerLayout->addLayout(actions);
    layout->addWidget(composer);
    privacy_ = textLabel({}, this, QStringLiteral("assistantPrivacy"));
    privacy_->setAlignment(Qt::AlignCenter);
    layout->addWidget(privacy_);

    connect(input_, &QPlainTextEdit::textChanged, this, &SupportPage::updateControls);
    connect(send_, &QPushButton::clicked, this, [this]() { submit(input_->toPlainText()); });
    connect(stop_, &QPushButton::clicked, &service_, &AssistantService::cancel);
    connect(newChat, &QPushButton::clicked, this, &SupportPage::resetConversation);
    connect(mode_, &QComboBox::currentIndexChanged, this, [this]() {
        if (mode_->currentIndex() == 0 && !service_.config().isReady()) {
            mode_->setCurrentIndex(1);
        }
        updateControls();
    });
    connect(&service_, &AssistantService::answerUpdated, this,
            [this](quint64 id, const QString &text) {
                if (id == activeId_ && pendingText_) { pendingText_->setText(text); }
            });
    connect(&service_, &AssistantService::finished, this, &SupportPage::complete);
    auto *bar = scroll_->verticalScrollBar();
    connect(bar, &QScrollBar::valueChanged, this, [this, bar](int value) {
        stickToBottom_ = value >= bar->maximum() - 24;
    });
    connect(bar, &QScrollBar::rangeChanged, this, [this]() {
        if (stickToBottom_) { scrollToBottom(); }
    });
    updateControls();
}

void SupportPage::scrollToBottom()
{
    QTimer::singleShot(0, this, [this]() {
        if (!stickToBottom_) { return; }
        auto *bar = scroll_->verticalScrollBar();
        const QSignalBlocker blocker(bar);
        bar->setValue(bar->maximum());
    });
}

QWidget *SupportPage::appendMessage(bool user, const QString &text, QLabel **body)
{
    auto *row = new QWidget(canvas_);
    row->setObjectName(QStringLiteral("assistantMessageRow"));
    auto *rowLayout = new QHBoxLayout(row);
    rowLayout->setContentsMargins(user ? 24 : 0, 0, user ? 0 : 12, 0);
    auto *bubble = new QFrame(row);
    bubble->setProperty("chatRole", user ? "user" : "assistant");
    auto *bubbleLayout = new QVBoxLayout(bubble);
    bubbleLayout->setContentsMargins(14, 12, 14, 12);
    bubbleLayout->setSpacing(8);
    auto *caption = textLabel(user ? QStringLiteral("你") : QStringLiteral("BIT 充电助理"), bubble);
    caption->setProperty("chatCaption", true);
    auto *label = textLabel(text, bubble);
    label->setProperty("chatBody", true);
    label->setObjectName(user ? QStringLiteral("assistantUserText") : QStringLiteral("assistantAnswerText"));
    label->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
    bubbleLayout->addWidget(caption);
    bubbleLayout->addWidget(label);
    rowLayout->addWidget(bubble);
    messagesLayout_->insertWidget(messagesLayout_->count() - 1, row);
    messages_.append(row);
    if (body) { *body = label; }
    return bubble;
}

void SupportPage::submit(const QString &question)
{
    const QString text = question.trimmed();
    if (activeId_ || text.isEmpty() || text.size() > 1200 || turnCount_ >= 24) { return; }
    const auto id = service_.ask(text, history_, mode_->currentIndex() == 0);
    if (id == 0) { return; }
    activeId_ = id;
    ++turnCount_;
    pendingQuestion_ = text;
    welcome_->hide();
    stickToBottom_ = true;
    appendMessage(true, text);
    pendingBubble_ = appendMessage(false, mode_->currentIndex() == 0
        ? QStringLiteral("正在检索知识并等待 AI 回复…")
        : QStringLiteral("正在查阅项目知识…"), &pendingText_);
    input_->clear();
    updateControls();
    scrollToBottom();
}

void SupportPage::complete(quint64 id, const AssistantResult &result)
{
    if (id != activeId_ || !pendingBubble_) { return; }
    activeId_ = 0;
    pendingText_->setText(result.answer.isEmpty() ? QStringLiteral("暂未生成回答") : result.answer);
    auto *layout = qobject_cast<QVBoxLayout *>(pendingBubble_->layout());
    auto *caption = textLabel(result.remote ? QStringLiteral("AI 回答 · %1").arg(service_.config().model)
                                            : QStringLiteral("本地知识摘录 · 未调用 AI"), pendingBubble_);
    caption->setProperty("chatCaption", true);
    layout->addWidget(caption);
    if (result.success) {
        history_.append({pendingQuestion_, result.answer});
        while (history_.size() > 4) { history_.removeFirst(); }
        auto *copy = new QToolButton(pendingBubble_);
        copy->setObjectName(QStringLiteral("assistantCopy"));
        copy->setText(QStringLiteral("复制回答"));
        connect(copy, &QToolButton::clicked, this, [copy, answer = result.answer]() {
            QApplication::clipboard()->setText(answer);
            copy->setText(QStringLiteral("已复制"));
        });
        layout->addWidget(copy, 0, Qt::AlignLeft);
    } else {
        auto *error = textLabel(result.error, pendingBubble_, QStringLiteral("assistantError"));
        layout->addWidget(error);
        auto *retry = new QToolButton(pendingBubble_);
        retry->setObjectName(QStringLiteral("assistantRetry"));
        retry->setText(result.cancelled ? QStringLiteral("重新提问 ↻") : QStringLiteral("重试这个问题 ↻"));
        connect(retry, &QToolButton::clicked, this,
                [this, question = pendingQuestion_]() { submit(question); });
        layout->addWidget(retry, 0, Qt::AlignLeft);
    }
    if (!result.sources.isEmpty()) {
        auto *toggle = new QToolButton(pendingBubble_);
        toggle->setObjectName(QStringLiteral("assistantSources"));
        toggle->setText(QStringLiteral("查看参考知识 · %1 条").arg(result.sources.size()));
        toggle->setCheckable(true);
        QStringList texts;
        for (const auto &entry : result.sources) {
            texts.append(QStringLiteral("[%1] %2\n%3\n来源：%4")
                             .arg(entry.id, entry.title, entry.content, entry.source));
        }
        auto *sources = textLabel(texts.join(QStringLiteral("\n\n")), pendingBubble_,
                                  QStringLiteral("assistantSourceText"));
        sources->setTextInteractionFlags(Qt::TextSelectableByMouse);
        sources->hide();
        connect(toggle, &QToolButton::toggled, sources, &QWidget::setVisible);
        layout->addWidget(toggle, 0, Qt::AlignLeft);
        layout->addWidget(sources);
    }
    pendingBubble_ = nullptr;
    pendingText_ = nullptr;
    pendingQuestion_.clear();
    updateControls();
    scrollToBottom();
    if (isVisible()) { input_->setFocus(); }
}

void SupportPage::updateControls()
{
    const bool busy = activeId_ != 0;
    const auto size = input_->toPlainText().size();
    send_->setEnabled(!busy && turnCount_ < 24 && size <= 1200
                      && !input_->toPlainText().trimmed().isEmpty());
    send_->setVisible(!busy);
    stop_->setVisible(busy);
    mode_->setEnabled(!busy);
    for (auto *button : suggestions_) { button->setEnabled(!busy); }
    counter_->setText(size > 1200 ? QStringLiteral("超过字数限制：%1 / 1200").arg(size)
                                : QStringLiteral("Shift+Enter 换行 · %1/1200").arg(size));
    if (busy) {
        status_->setText(QStringLiteral("正在回答…"));
    } else if (turnCount_ >= 24) {
        status_->setText(QStringLiteral("本次对话已达上限，请新建对话。"));
    } else if (mode_->currentIndex() == 0) {
        status_->setText(QStringLiteral("%1 · 按需连接").arg(service_.config().model));
    } else {
        status_->setText(service_.config().isReady() ? QStringLiteral("离线可用 · 不调用 AI")
                                                    : service_.config().validationError());
    }
    privacy_->setText(mode_->currentIndex() == 0
        ? QStringLiteral("AI 会发送聊天与知识片段，勿输入隐私。以业务页面为准。")
        : QStringLiteral("仅查阅本地项目知识，不联网。实时信息以业务页面为准。"));
}

void SupportPage::resetConversation()
{
    activeId_ = 0; // Invalidate before synchronous cancellation delivery.
    service_.cancel();
    pendingText_ = nullptr;
    pendingBubble_ = nullptr;
    pendingQuestion_.clear();
    history_.clear();
    turnCount_ = 0;
    for (auto *message : messages_) {
        messagesLayout_->removeWidget(message);
        message->hide();
        message->deleteLater();
    }
    messages_.clear();
    welcome_->show();
    input_->clear();
    stickToBottom_ = true;
    updateControls();
    if (isVisible()) { input_->setFocus(); }
}

}  // namespace charging::client
