// 管理台工单页面：分页浏览用户工单并保存处理结果与回复
#include "admin_ui/support_tickets_page.h"
#include "admin_ui/admin_facade.h"
#include "charging/protocol/protocol_constants.h"

#include "admin_combo_box.h"
#include <QFrame>
#include "admin_time_format.h"
#include <QHBoxLayout>
#include <QJsonArray>
#include <QLabel>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace charging::server {
using namespace charging::protocol;

// 构造页面布局：左侧工单列表，右侧详情与处理回复区
SupportTicketsPage::SupportTicketsPage(AdminFacade *facade, QWidget *parent)
    : QWidget(parent), facade_(facade)
{
    setObjectName(QStringLiteral("supportTicketsPage"));
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(16);
    auto *body = new QHBoxLayout;
    body->setSpacing(16);
    auto *inbox = new QFrame(this);
    inbox->setObjectName("panel");
    inbox->setMinimumWidth(280);
    inbox->setMaximumWidth(360);
    auto *inboxLayout = new QVBoxLayout(inbox);
    inboxLayout->setContentsMargins(16, 18, 16, 14);
    inboxLayout->setSpacing(12);
    auto *inboxHeader = new QHBoxLayout;
    count_ = new QLabel(QStringLiteral("全部工单"), inbox);
    count_->setObjectName("adminTicketCount");
    count_->setProperty("role", "sectionTitle");
    inboxHeader->addWidget(count_, 1);
    inboxLayout->addLayout(inboxHeader);
    list_ = new QListWidget(inbox);
    list_->setObjectName(QStringLiteral("adminTicketList"));
    list_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    list_->setTextElideMode(Qt::ElideRight);
    list_->setMouseTracking(true);
    list_->setSpacing(2);
    inboxLayout->addWidget(list_, 1);
    // 加载更多按钮按游标继续取下一页工单
    more_ = new QPushButton(QStringLiteral("加载更多"), inbox);
    more_->setObjectName(QStringLiteral("adminTicketMore"));
    inboxLayout->addWidget(more_);
    body->addWidget(inbox, 2);

    auto *detail = new QVBoxLayout;
    detail->setSpacing(16);
    auto *description = new QFrame(this);
    description->setObjectName("panel");
    auto *descriptionLayout = new QVBoxLayout(description);
    descriptionLayout->setContentsMargins(20, 18, 20, 18);
    auto *detailTitle = new QLabel(QStringLiteral("问题详情"), description);
    detailTitle->setProperty("role", "sectionTitle");
    auto *detailHeader = new QHBoxLayout;
    detailHeader->addWidget(detailTitle, 1);
    locatePile_ = new QPushButton(QStringLiteral("定位电桩"), description);
    locatePile_->setObjectName(QStringLiteral("adminTicketLocatePile"));
    detailHeader->addWidget(locatePile_);
    descriptionLayout->addLayout(detailHeader);
    // 报修工单可一键跳到充电桩管理定位该桩
    connect(locatePile_, &QPushButton::clicked, this, [this] {
        const int row = list_->currentRow();
        if (row >= 0 && row < tickets_.size() && !tickets_[row].pileCode.isEmpty())
            emit locatePileRequested(tickets_[row].pileCode);
    });
    summary_ = new QPlainTextEdit(description);
    summary_->setObjectName(QStringLiteral("adminTicketSummary"));
    summary_->setReadOnly(true);
    summary_->setPlaceholderText(QStringLiteral("暂无工单，收到用户反馈后会在这里显示"));
    descriptionLayout->addWidget(summary_, 1);
    detail->addWidget(description, 1);
    auto *handling = new QFrame(this);
    handling->setObjectName("panel");
    auto *handlingLayout = new QVBoxLayout(handling);
    handlingLayout->setContentsMargins(20, 18, 20, 18);
    handlingLayout->setSpacing(10);
    auto *handlingHeader = new QHBoxLayout;
    auto *handlingTitle = new QLabel(QStringLiteral("处理与回复"), handling);
    handlingTitle->setProperty("role", "sectionTitle");
    handlingHeader->addWidget(handlingTitle, 1);
    // 处理状态下拉：待处理、处理中、已解决
    auto *statusLabel = new QLabel(QStringLiteral("处理状态"), handling);
    handlingHeader->addWidget(statusLabel);
    status_ = new AdminComboBox(handling);
    status_->setObjectName(QStringLiteral("adminTicketStatus"));
    for (auto status : {TicketStatus::Open, TicketStatus::InProgress, TicketStatus::Resolved})
        status_->addItem(ticketStatusLabel(status), toString(status));
    statusLabel->setBuddy(status_);
    handlingHeader->addWidget(status_);
    handlingLayout->addLayout(handlingHeader);
    reply_ = new QPlainTextEdit(handling);
    reply_->setObjectName(QStringLiteral("adminTicketReply"));
    reply_->setAccessibleName(QStringLiteral("回复用户"));
    reply_->setPlaceholderText(QStringLiteral("填写处理情况，用户可在‘我的工单’查看回复"));
    reply_->setMinimumHeight(110);
    handlingLayout->addWidget(reply_, 1);
    auto *footer = new QHBoxLayout;
    auto *limit = new QLabel(QStringLiteral("0 / 2000 字 · 已解决时须填写回复"), handling);
    limit->setProperty("role", "muted");
    footer->addWidget(limit, 1);
    connect(reply_, &QPlainTextEdit::textChanged, this, [this, limit] {
        limit->setText(QStringLiteral("%1 / 2000 字 · 已解决时须填写回复").arg(reply_->toPlainText().size()));
    });
    save_ = new QPushButton(QStringLiteral("保存处理结果"), handling);
    save_->setObjectName(QStringLiteral("adminTicketSave"));
    save_->setProperty("primary", true);
    save_->setMinimumHeight(28);
    footer->addWidget(save_);
    handlingLayout->addLayout(footer);
    detail->addWidget(handling, 1);
    body->addLayout(detail, 4);
    root->addLayout(body, 1);
    notice_ = new QLabel(this);
    notice_->setObjectName(QStringLiteral("adminTicketNotice"));
    notice_->setTextFormat(Qt::PlainText);
    notice_->setWordWrap(true);
    notice_->setMinimumHeight(34);
    root->addWidget(notice_);
    connect(more_, &QPushButton::clicked, this, [this] { refresh(true); });
    connect(list_, &QListWidget::currentRowChanged, this, [this] { selectTicket(); });
    connect(save_, &QPushButton::clicked, this, [this] { save(); });
    clear();
}

// 清空列表与编辑区，禁用操作按钮回到初始态
void SupportTicketsPage::clear()
{
    tickets_.clear(); list_->clear(); summary_->clear(); reply_->clear();
    hasMore_ = false; more_->setEnabled(false); save_->setEnabled(false);
    locatePile_->hide(); locatePile_->setEnabled(false); locatePile_->setToolTip({});
    status_->setCurrentIndex(0);
    status_->setEnabled(false); reply_->setEnabled(false);
    count_->setText(QStringLiteral("全部工单")); notice_->clear();
}

// 拉取工单列表；more 为真时以最后一条ID继续分页
void SupportTicketsPage::refresh(bool more)
{
    if (more && (!hasMore_ || tickets_.isEmpty())) return;
    const auto result = facade_->listSupportTickets(more
        ? std::optional<qint64>(tickets_.last().ticketId) : std::nullopt);
    // 读取失败时区分未启用工单功能与登录/服务异常提示
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
        if (!ticket.pileCode.isEmpty()) list_->item(list_->count() - 1)->setText(
            QStringLiteral("[报修 · %1] ").arg(ticket.pileCode) + list_->item(list_->count() - 1)->text());
    }
    hasMore_ = result.data.value("hasMore").toBool();
    more_->setEnabled(hasMore_);
    count_->setText(QStringLiteral("已加载 %1 张").arg(tickets_.size()));
    notice_->setText(tickets_.isEmpty() ? QStringLiteral("暂无工单") : QString());
    for (int row = 0; row < list_->count(); ++row) {
        list_->item(row)->setSizeHint(QSize(0, 88));
        list_->item(row)->setToolTip(list_->item(row)->text());
    }
    if (list_->currentRow() < 0 && list_->count()) list_->setCurrentRow(0);
}

qint64 SupportTicketsPage::selectedTicketId() const
{
    const int row = list_->currentRow();
    return row >= 0 && row < tickets_.size() ? tickets_[row].ticketId : 0;
}

// 刷新后尝试重新选中原工单，必要时继续翻页查找
void SupportTicketsPage::restoreTicketSelection(qint64 ticketId)
{
    refresh(); // Recheck authorization and current data before restoring a historical selection.
    if (ticketId <= 0) return;
    while (!tickets_.isEmpty()) {
        for (int row = 0; row < tickets_.size(); ++row) {
            if (tickets_[row].ticketId == ticketId) {
                list_->setCurrentRow(row);
                list_->scrollToItem(list_->item(row));
                return;
            }
        }
        // Pages are ordered by descending ID. Stop if the old item is gone.
        if (!hasMore_ || tickets_.last().ticketId < ticketId) return;
        const int previousCount = tickets_.size();
        refresh(true);
        if (tickets_.size() <= previousCount) return;
    }
}

// 切换选中工单时填充详情、状态与既有回复
void SupportTicketsPage::selectTicket()
{
    const int row = list_->currentRow();
    const bool valid = row >= 0 && row < tickets_.size();
    save_->setEnabled(valid);
    const bool hasPile = valid && !tickets_[row].pileCode.isEmpty();
    locatePile_->setVisible(hasPile);
    locatePile_->setEnabled(hasPile);
    locatePile_->setToolTip(hasPile ? QStringLiteral("在充电桩管理中定位 %1").arg(tickets_[row].pileCode) : QString());
    status_->setEnabled(valid); reply_->setEnabled(valid);
    if (!valid) { summary_->clear(); reply_->clear(); return; }
    const auto &ticket = tickets_[row];
    summary_->setPlainText(QStringLiteral("#%1 · %2\n用户 #%3 · %4\n\n%5\n\n最后更新：%6")
        .arg(ticket.ticketId).arg(ticket.title).arg(ticket.userId).arg(adminTimeText(ticket.createdAt),
             ticket.summary, adminTimeText(ticket.updatedAt)));
    if (!ticket.pileCode.isEmpty()) summary_->appendPlainText(
        QStringLiteral("\n充电桩报修\n桩编号：%1\n故障类型：%2")
            .arg(ticket.pileCode, ticket.faultType));
    status_->setCurrentIndex(status_->findData(toString(ticket.status)));
    reply_->setPlainText(ticket.reply);
}

// 提交处理结果，成功后用服务端返回的工单刷新本地显示
void SupportTicketsPage::save()
{
    const int row = list_->currentRow();
    if (row < 0 || row >= tickets_.size()) return;
    const auto result = facade_->updateSupportTicket({{"ticketId", tickets_[row].ticketId},
        {"status", status_->currentData().toString()}, {"reply", reply_->toPlainText().trimmed()}});
    if (!result.ok()) {
        if (result.code == ErrorCode::Forbidden || result.code == ErrorCode::InvalidSession) clear();
        notice_->setText(QStringLiteral("保存失败：回复最多 2000 字，‘已解决’需填写回复；请确认仍已登录。")); return;
    }
    SupportTicketDto updated;
    if (!fromJson(result.data.value("ticket").toObject(), &updated)) {
        notice_->setText(QStringLiteral("保存结果格式异常，请刷新核对。")); return;
    }
    tickets_[row] = updated;
    list_->item(row)->setText(QStringLiteral("#%1  %2\n用户 #%3 · %4")
        .arg(updated.ticketId).arg(updated.title).arg(updated.userId).arg(ticketStatusLabel(updated.status)));
    if (!updated.pileCode.isEmpty()) list_->item(row)->setText(
        QStringLiteral("[报修 · %1] ").arg(updated.pileCode) + list_->item(row)->text());
    selectTicket();
    notice_->setText(QStringLiteral("工单 #%1 处理结果已保存，用户刷新‘我的工单’即可查看。").arg(updated.ticketId));
}

}  // namespace charging::server
