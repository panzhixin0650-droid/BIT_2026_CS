#pragma once

#include "assistant/assistant_service.h"

#include <QWidget>

class QComboBox;
class QLabel;
class QPlainTextEdit;
class QPushButton;
class QScrollArea;
class QVBoxLayout;

namespace charging::client {

class BusyIndicator;

// 本文件声明AI助手页控件与内部状态
class SupportPage final : public QWidget {
    Q_OBJECT
public:
    explicit SupportPage(AssistantService &service, QWidget *parent = nullptr);
    // 对外提供重置对话与读取最近几轮历史
    void resetConversation();
    QList<AssistantTurn> recentHistory() const { return history_; }
signals:
    // 请求转到模拟客服与工单页
    void supportDeskRequested();
protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    // 私有流程：提交提问、刷新控件、处理回答完成
    void submit(const QString &question);
    void updateControls();
    void complete(quint64 id, const AssistantResult &result);
    QWidget *appendMessage(bool user, const QString &text, QLabel **body = nullptr);
    void scrollToBottom();

    AssistantService &service_;
    QComboBox *mode_ = nullptr;
    QLabel *status_ = nullptr;
    BusyIndicator *busyIndicator_ = nullptr;
    QTimer waitingTimer_;
    QLabel *privacy_ = nullptr;
    QScrollArea *scroll_ = nullptr;
    QWidget *canvas_ = nullptr;
    QWidget *welcome_ = nullptr;
    QVBoxLayout *messagesLayout_ = nullptr;
    QPlainTextEdit *input_ = nullptr;
    QLabel *counter_ = nullptr;
    QPushButton *send_ = nullptr;
    QPushButton *stop_ = nullptr;
    QPushButton *deskEntry_ = nullptr;
    QList<QPushButton *> suggestions_;
    QList<QWidget *> messages_;
    QList<AssistantTurn> history_;
    // 以下为控件指针、对话历史与当前请求状态
    quint64 activeId_ = 0;
    QString pendingQuestion_;
    QWidget *pendingBubble_ = nullptr;
    QLabel *pendingText_ = nullptr;
    bool stickToBottom_ = true;
    int turnCount_ = 0;
};

}  // namespace charging::client
