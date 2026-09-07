#include "admin_ui/support_tickets_page.h"
#include "admin_ui/admin_facade.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QLabel>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace charging::server {
using namespace charging::protocol;

SupportTicketsPage::SupportTicketsPage(AdminFacade *facade, QWidget *parent)
    : QWidget(parent), facade_(facade)
{
    setObjectName(QStringLiteral("supportTicketsPage"));
    auto *root = new QVBoxLayout(this);
    notice_ = new QLabel(QStringLiteral("用户确认提交的工单；回复和状态只影响工单，不会修改订单或余额。"), this);
    notice_->setObjectName(QStringLiteral("adminTicketNotice"));
    notice_->setTextFormat(Qt::PlainText);
    notice_->setWordWrap(true);
    root->addWidget(notice_);
    auto *actions = new QHBoxLayout;
    auto *refreshButton = new QPushButton(QStringLiteral("刷新工单"), this);
    refreshButton->setObjectName(QStringLiteral("adminTicketRefresh"));
    more_ = new QPushButton(QStringLiteral("加载更多"), this);
    more_->setObjectName(QStringLiteral("adminTicketMore"));
    actions->addWidget(refreshButton); actions->addStretch(); actions->addWidget(more_);
    root->addLayout(actions);
    auto *body = new QHBoxLayout;
    list_ = new QListWidget(this);
    list_->setObjectName(QStringLiteral("adminTicketList"));
    body->addWidget(list_, 1);
    auto *detail = new QVBoxLayout;
    detail->addWidget(new QLabel(QStringLiteral("工单详情"), this));
    summary_ = new QPlainTextEdit(this);
    summary_->setObjectName(QStringLiteral("adminTicketSummary"));
    summary_->setReadOnly(true);
    summary_->setPlaceholderText(QStringLiteral("选择工单查看用户问题"));
    detail->addWidget(summary_, 1);
    status_ = new QComboBox(this);
    status_->setObjectName(QStringLiteral("adminTicketStatus"));
    for (auto status : {TicketStatus::Open, TicketStatus::InProgress, TicketStatus::Resolved})
        status_->addItem(ticketStatusLabel(status), toString(status));
    detail->addWidget(new QLabel(QStringLiteral("处理状态"), this));
    detail->addWidget(status_);
    reply_ = new QPlainTextEdit(this);
    reply_->setObjectName(QStringLiteral("adminTicketReply"));
    reply_->setPlaceholderText(QStringLiteral("管理员回复（最多 2000 字；标记已解决时必填）"));
    detail->addWidget(new QLabel(QStringLiteral("回复用户"), this));
    detail->addWidget(reply_, 1);
    save_ = new QPushButton(QStringLiteral("保存处理结果"), this);
    save_->setObjectName(QStringLiteral("adminTicketSave"));
    save_->setProperty("primary", true);
    detail->addWidget(save_);
    body->addLayout(detail, 2);
    root->addLayout(body, 1);
    connect(refreshButton, &QPushButton::clicked, this, [this] { refresh(); });
    connect(more_, &QPushButton::clicked, this, [this] { refresh(true); });
    connect(list_, &QListWidget::currentRowChanged, this, [this] { selectTicket(); });
    connect(save_, &QPushButton::clicked, this, [this] { save(); });
    clear();
}

void SupportTicketsPage::clear()
{
    tickets_.clear(); list_->clear(); summary_->clear(); reply_->clear();
    hasMore_ = false; more_->setEnabled(false); save_->setEnabled(false);
    status_->setCurrentIndex(0);
}

void SupportTicketsPage::refresh(bool more)
{
    if (more && (!hasMore_ || tickets_.isEmpty())) return;
    const auto result = facade_->listSupportTickets(more
        ? std::optional<qint64>(tickets_.last().ticketId) : std::nullopt);
    if (!result.ok()) {
        clear();
        notice_->setText(result.message == QStringLiteral("SUPPORT_TICKETS_MIGRATION_REQUIRED")
            ? QStringLiteral("工单功能尚未启用，请联系维护人员升级；其他业务可继续使用。")
            : QStringLiteral("无法读取工单，请确认管理员已登录，或联系维护人员检查服务。"));
        return;
    }
    if (!more) clear();
    for (const auto &value : result.data.value("items").toArray()) {
        SupportTicketDto ticket;
        if (!fromJson(value.toObject(), &ticket)) { clear(); notice_->setText(QStringLiteral("工单数据异常")); return; }
        tickets_.append(ticket);
        list_->addItem(QStringLiteral("#%1  %2\n用户 #%3 · %4")
            .arg(ticket.ticketId).arg(ticket.title).arg(ticket.userId).arg(ticketStatusLabel(ticket.status)));
    }
    hasMore_ = result.data.value("hasMore").toBool();
    more_->setEnabled(hasMore_);
    notice_->setText(QStringLiteral("已加载 %1 张工单。回复和处理状态仅影响工单，不会自动退款或修改订单。")
                     .arg(tickets_.size()));
    if (list_->currentRow() < 0 && list_->count()) list_->setCurrentRow(0);
}

void SupportTicketsPage::selectTicket()
{
    const int row = list_->currentRow();
    const bool valid = row >= 0 && row < tickets_.size();
    save_->setEnabled(valid);
    if (!valid) { summary_->clear(); reply_->clear(); return; }
    const auto &ticket = tickets_[row];
    summary_->setPlainText(QStringLiteral("#%1 · %2\n用户 #%3 · %4\n摘要来源：%5\n\n%6\n\n最后更新：%7")
        .arg(ticket.ticketId).arg(ticket.title).arg(ticket.userId).arg(ticket.createdAt,
             ticket.sourceModel.isEmpty() ? QStringLiteral("手动填写") : ticket.sourceModel,
             ticket.summary, ticket.updatedAt));
    status_->setCurrentIndex(status_->findData(toString(ticket.status)));
    reply_->setPlainText(ticket.reply);
}

void SupportTicketsPage::save()
{
    const int row = list_->currentRow();
    if (row < 0 || row >= tickets_.size()) return;
    const auto result = facade_->updateSupportTicket({{"ticketId", tickets_[row].ticketId},
        {"status", status_->currentData().toString()}, {"reply", reply_->toPlainText().trimmed()}});
    if (!result.ok()) {
        notice_->setText(QStringLiteral("保存失败：回复最多 2000 字，‘已解决’需填写回复；请确认仍已登录。")); return;
    }
    SupportTicketDto updated;
    if (!fromJson(result.data.value("ticket").toObject(), &updated)) {
        notice_->setText(QStringLiteral("保存结果格式异常，请刷新核对。")); return;
    }
    tickets_[row] = updated;
    list_->item(row)->setText(QStringLiteral("#%1  %2\n用户 #%3 · %4")
        .arg(updated.ticketId).arg(updated.title).arg(updated.userId).arg(ticketStatusLabel(updated.status)));
    selectTicket();
    notice_->setText(QStringLiteral("工单 #%1 处理结果已保存，用户刷新‘我的工单’即可查看。").arg(updated.ticketId));
}

}  // namespace charging::server
