// 单价旁的问号按钮：点击弹出计价规则说明
#include "ui/pricing_info_button.h"

#include <QDialog>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

namespace charging::client {

// 构造成小圆问号按钮，无规则时隐藏
PricingInfoButton::PricingInfoButton(QWidget *parent) : QToolButton(parent)
{
    setText(QStringLiteral("?"));
    setProperty("role", "pricingHelp");
    setAccessibleName(QStringLiteral("查看计价规则"));
    setToolTip(accessibleName());
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::StrongFocus);
    setFixedSize(28, 28);
    hide();
    connect(this, &QToolButton::clicked, this, &PricingInfoButton::showRules);
}

// 规则文本变化时先关闭旧弹窗再更新
void PricingInfoButton::setRules(const QString &rules)
{
    if (rules_ != rules) {
        closeRules();
        rules_ = rules;
    }
    setVisible(!rules_.isEmpty());
}

// 以非模态对话框展示规则，避免阻塞刷新
void PricingInfoButton::showRules()
{
    if (rules_.isEmpty() || !isVisible() || dialog_) return;
    auto *dialog = new QDialog(this);
    dialog_ = dialog;
    dialog->setObjectName(QStringLiteral("pricingRulesDialog"));
    dialog->setWindowTitle(QStringLiteral("计价规则"));
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setFixedWidth(qBound(240, window()->width() - 32, 380));
    auto *layout = new QVBoxLayout(dialog);
    layout->setContentsMargins(20, 20, 20, 20);
    layout->setSpacing(16);
    auto *title = new QLabel(QStringLiteral("计价规则"), dialog);
    title->setProperty("role", "sectionTitle");
    auto *text = new QLabel(rules_, dialog);
    text->setObjectName(QStringLiteral("pricingRulesText"));
    text->setTextFormat(Qt::PlainText);
    text->setWordWrap(true);
    // Resolve wrapping at the actual dialog width before its initial size hint.
    // Otherwise a narrow window can retain the single-line label height.
    text->setFixedWidth(dialog->width() - 40);
    text->ensurePolished();
    text->setMinimumHeight(text->heightForWidth(text->width()));
    auto *close = new QPushButton(QStringLiteral("知道了"), dialog);
    close->setObjectName(QStringLiteral("pricingRulesCloseButton"));
    close->setProperty("role", "primary");
    close->setDefault(true);
    layout->addWidget(title);
    layout->addWidget(text);
    layout->addWidget(close);
    connect(close, &QPushButton::clicked, dialog, &QDialog::accept);
    // No nested event loop: quotes, session refreshes and logout keep working.
    dialog->open();
}

// 主动关闭已打开的规则弹窗
void PricingInfoButton::closeRules()
{
    if (!dialog_) return;
    dialog_->close();
    dialog_.clear();
}

// 按钮隐藏时同步关闭弹窗
void PricingInfoButton::hideEvent(QHideEvent *event)
{
    closeRules();
    QToolButton::hideEvent(event);
}

}  // namespace charging::client
