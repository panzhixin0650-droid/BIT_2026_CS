// 管理台工单页面声明：列表、详情与回复保存
#pragma once

#include "charging/protocol/support_ticket.h"
#include <QWidget>
#include <QList>

class QComboBox;
class QLabel;
class QListWidget;
class QPlainTextEdit;
class QPushButton;

namespace charging::server {
class AdminFacade;

// 工单处理页面控件，数据经 AdminFacade 获取
class SupportTicketsPage final : public QWidget {
    Q_OBJECT
public:
    explicit SupportTicketsPage(AdminFacade *facade, QWidget *parent = nullptr);
    // 刷新列表，more 为真表示追加下一页
    void refresh(bool more = false);
    void clear();
    qint64 selectedTicketId() const;
    void restoreTicketSelection(qint64 ticketId);
signals:
    // 请求主窗口跳转到对应电桩的信号
    void locatePileRequested(const QString &pileCode);
private:
    void selectTicket();
    void save();
    // 页面持有的控件、已加载工单缓存与是否还有下一页
    AdminFacade *facade_;
    QListWidget *list_;
    QPlainTextEdit *summary_;
    QPlainTextEdit *reply_;
    QComboBox *status_;
    QLabel *notice_;
    QLabel *count_;
    QPushButton *save_;
    QPushButton *more_;
    QPushButton *locatePile_;
    QList<charging::protocol::SupportTicketDto> tickets_;
    bool hasMore_ = false;
};
}  // namespace charging::server
