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

class SupportPage final : public QWidget {
    Q_OBJECT
public:
    explicit SupportPage(AssistantService &service, QWidget *parent = nullptr);
    void resetConversation();

private:
    void submit(const QString &question);
    void updateControls();
    void complete(quint64 id, const AssistantResult &result);
    QWidget *appendMessage(bool user, const QString &text, QLabel **body = nullptr);
    void scrollToBottom();

    AssistantService &service_;
    QComboBox *mode_ = nullptr;
    QLabel *status_ = nullptr;
    QLabel *privacy_ = nullptr;
    QScrollArea *scroll_ = nullptr;
    QWidget *canvas_ = nullptr;
    QWidget *welcome_ = nullptr;
    QVBoxLayout *messagesLayout_ = nullptr;
    QPlainTextEdit *input_ = nullptr;
    QLabel *counter_ = nullptr;
    QPushButton *send_ = nullptr;
    QPushButton *stop_ = nullptr;
    QList<QPushButton *> suggestions_;
    QList<QWidget *> messages_;
    QList<AssistantTurn> history_;
    quint64 activeId_ = 0;
    QString pendingQuestion_;
    QWidget *pendingBubble_ = nullptr;
    QLabel *pendingText_ = nullptr;
    bool stickToBottom_ = true;
    int turnCount_ = 0;
};

}  // namespace charging::client
