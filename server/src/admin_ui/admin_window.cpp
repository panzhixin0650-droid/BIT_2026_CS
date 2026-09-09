#include "admin_window.h"

#include "admin_facade.h"
#include "support_tickets_page.h"
#include "pile_status_chart.h"
#include "revenue_chart.h"
#include "analysis_bar_chart.h"
#include "admin_analytics.h"
#include "admin_time_format.h"
#include "animated_metric_label.h"
#include <QTimeZone>
#include <algorithm>

#include "charging/protocol/dto.h"
#include "charging/protocol/protocol_constants.h"

#include "admin_combo_box.h"
#include <QKeyEvent>
#include <QCheckBox>
#include <QRadioButton>
#include <QButtonGroup>
#include <QDateTimeEdit>
#include <QApplication>
#include <QClipboard>
#include <QDateTime>
#include <QDebug>
#include <QDialog>
#include <QVariantAnimation>
#include <QRadialGradient>
#include <QDialogButtonBox>
#include <QDateEdit>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QEventLoop>
#include <QFrame>
#include <QGridLayout>
#include <QHeaderView>
#include <QHash>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QMenu>
#include <QPixmap>
#include <QPainter>
#include <QPainterPath>
#include <QLinearGradient>
#include <QResizeEvent>
#include <QPaintEvent>
#include <QSet>
#include <QSignalBlocker>
#include <QScrollArea>
#include <QScrollBar>
#include <QScreen>
#include <QShortcut>
#include <QSizePolicy>
#include <QStyle>
#include <QTimer>
#include <QToolButton>
#include <QSpinBox>
#include <QStackedWidget>
#include <QStyledItemDelegate>
#include <QTableWidget>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QMouseEvent>

#include <utility>
#include <functional>

namespace charging::server {
namespace {

using namespace charging::protocol;

QLabel *heading(const QString &text, QWidget *parent, const char *role = "sectionTitle")
{
    auto *label = new QLabel(text, parent);
    label->setProperty("role", role);
    return label;
}

// A light content-wide reveal provides refresh feedback without capturing the
// whole page into an offscreen opacity effect or blocking pointer events.
class PageRefreshFeedback final : public QWidget {
public:
    explicit PageRefreshFeedback(QWidget *parent) : QWidget(parent), animation_(this)
    {
        setObjectName(QStringLiteral("pageRefreshFeedback"));
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setAttribute(Qt::WA_NoSystemBackground);
        setFocusPolicy(Qt::NoFocus);
        animation_.setObjectName(QStringLiteral("adminPageRefreshAnimation"));
        animation_.setDuration(420);
        animation_.setStartValue(0.0);
        animation_.setEndValue(1.0);
        animation_.setEasingCurve(QEasingCurve::OutCubic);
        connect(&animation_, &QVariantAnimation::valueChanged, this, [this] { update(); });
        connect(&animation_, &QVariantAnimation::finished, this, &QWidget::hide);
        parent->installEventFilter(this);
        hide();
    }
    void replay()
    {
        animation_.stop();
        setGeometry(parentWidget()->rect());
        show(); raise();
        animation_.start();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        const qreal progress = animation_.currentValue().toReal();
        QPainter painter(this);
        painter.fillRect(rect(), QColor(243, 246, 251, qRound(170*(1-progress))));
        painter.fillRect(QRectF(0, 0, width()*progress, 3),
                         QColor(47, 111, 237, qRound(255*(1-progress))));
    }
    void hideEvent(QHideEvent *event) override
    {
        animation_.stop();
        QWidget::hideEvent(event);
    }
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (event->type() == QEvent::Resize && watched == parentWidget())
            setGeometry(parentWidget()->rect());
        return QWidget::eventFilter(watched, event);
    }

private:
    QVariantAnimation animation_;
};

class LoginBackdrop final : public QWidget {
public:
    using QWidget::QWidget;

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        QLinearGradient base(0, 0, width(), height());
        base.setColorAt(0, QColor("#dce8fb"));
        base.setColorAt(0.5, QColor("#f2f7fe"));
        base.setColorAt(1, QColor("#d8e6f8"));
        painter.fillRect(rect(), base);
        const qreal unit = qMax(width(), height());
        const auto glow = [&](QPointF center, QColor color) {
            QRadialGradient gradient(center, unit*0.42);
            gradient.setColorAt(0, color);
            color.setAlpha(0); gradient.setColorAt(1, color);
            painter.fillRect(rect(), gradient);
        };
        glow(QPointF(width()*0.9, height()*0.1), QColor(83,146,241,90));
        glow(QPointF(width()*0.05, height()*0.95), QColor(96,161,222,82));
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(102,142,197,56));
        for (int y=30; y<height(); y+=36)
            for (int x=30; x<width(); x+=36) painter.drawEllipse(QPointF(x,y),0.8,0.8);
        // Flowing circuit paths remain behind the opaque login card.
        for (int i=0; i<4; ++i) {
            const qreal offset = i*30;
            QPainterPath path;
            path.moveTo(-60, height()*0.8+offset);
            path.cubicTo(width()*0.28, height()*0.8+offset,
                         width()*0.1, height()*0.15+offset, width()*0.45, height()*0.2+offset);
            path.cubicTo(width()*0.78, height()*0.24+offset,
                         width()*0.82, -80+offset, width()+60, 20+offset);
            painter.setBrush(Qt::NoBrush);
            painter.setPen(QPen(QColor(72,124,196,85-i*12),1.5));
            painter.drawPath(path);
        }
        for (const QPointF &center : {QPointF(width()*0.1,height()*0.78),
                                      QPointF(width()*0.9,height()*0.14)}) {
            painter.setPen(QPen(QColor(93,144,211,115),1.5));
            painter.setBrush(QColor(255,255,255,165));
            painter.drawEllipse(center,18,18);
            painter.setPen(QPen(QColor(63,114,183,160),2,Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin));
            QPainterPath bolt;
            bolt.moveTo(center+QPointF(2,-9)); bolt.lineTo(center+QPointF(-5,1));
            bolt.lineTo(center+QPointF(3,1)); bolt.lineTo(center+QPointF(-2,9));
            painter.drawPath(bolt);
        }
    }
};

// Vector brand artwork stays crisp at desktop scale and never overlaps the form.
class LoginBrandPanel final : public QWidget {
public:
    explicit LoginBrandPanel(QWidget *parent) : QWidget(parent) {}
protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        QPainterPath shape;
        shape.setFillRule(Qt::WindingFill);
        shape.addRoundedRect(QRectF(rect()), 20, 20);
        shape.addRect(QRectF(width() - 20, 0, 20, height()));
        painter.setClipPath(shape);
        QLinearGradient gradient(0, 0, width(), height());
        gradient.setColorAt(0, QColor("#174a9b"));
        gradient.setColorAt(1, QColor("#102a56"));
        painter.fillRect(rect(), gradient);
        painter.setPen(QPen(QColor(255, 255, 255, 32), 1));
        for (int radius : {180, 250, 320})
            painter.drawEllipse(QPointF(width() + 30, height() + 10), radius, radius);
    }
};

class LoginLogo final : public QWidget {
public:
    explicit LoginLogo(QWidget *parent) : QWidget(parent)
    {
        setFixedSize(152, 152);
        setAccessibleName(QStringLiteral("悦充闪电标识"));
    }
protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor("#ffffff"));
        painter.drawRoundedRect(QRectF(4, 4, 144, 144), 36, 36);
        QPainterPath bolt;
        bolt.moveTo(86, 26); bolt.lineTo(47, 84); bolt.lineTo(72, 84);
        bolt.lineTo(64, 126); bolt.lineTo(109, 65); bolt.lineTo(83, 65);
        bolt.closeSubpath();
        painter.fillPath(bolt, QColor("#245fc3"));
    }
};

class LoginPasswordEdit final : public QLineEdit {
public:
    explicit LoginPasswordEdit(QWidget *parent) : QLineEdit(parent)
    {
        setEchoMode(QLineEdit::Password);
        setTextMargins(0, 0, 58, 0);
        toggle_ = new QToolButton(this);
        toggle_->setObjectName("loginPasswordToggle");
        toggle_->setText(QStringLiteral("显示"));
        toggle_->setAccessibleName(QStringLiteral("显示密码"));
        toggle_->setCheckable(true);
        toggle_->setFocusPolicy(Qt::StrongFocus);
        toggle_->setCursor(Qt::PointingHandCursor);
        connect(toggle_, &QToolButton::toggled, this, [this](bool visible) {
            setEchoMode(visible ? QLineEdit::Normal : QLineEdit::Password);
            toggle_->setText(visible ? QStringLiteral("隐藏") : QStringLiteral("显示"));
            toggle_->setAccessibleName(visible ? QStringLiteral("隐藏密码") : QStringLiteral("显示密码"));
            toggle_->setToolTip(toggle_->accessibleName());
        });
        connect(this, &QLineEdit::textChanged, this, [this](const QString &text) {
            if (text.isEmpty()) toggle_->setChecked(false);
        });
    }
protected:
    void resizeEvent(QResizeEvent *event) override
    {
        QLineEdit::resizeEvent(event);
        toggle_->setGeometry(width() - 62, 6, 54, height() - 12);
    }
private:
    QToolButton *toggle_;
};

constexpr int navigationGroupRole = Qt::UserRole + 8;

class NavigationDelegate final : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    QSize sizeHint(const QStyleOptionViewItem &, const QModelIndex &index) const override
    {
        return QSize(184, index.data(navigationGroupRole).toString().isEmpty() ? 46 : 76);
    }
    void paint(QPainter *p, const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        p->save();
        p->setRenderHint(QPainter::Antialiasing);
        p->fillRect(option.rect, QColor("#102a56"));
        QRect row = option.rect.adjusted(4, 3, -4, -3);
        const QString group = index.data(navigationGroupRole).toString();
        if (!group.isEmpty()) {
            QFont font = option.font; font.setPixelSize(12);
            p->setFont(font); p->setPen(QColor("#a2b5d2"));
            p->drawText(row.adjusted(14, 0, 0, 0), Qt::AlignLeft | Qt::AlignTop, group);
            row.setTop(row.top() + 30);
        }
        const bool selected = option.state & QStyle::State_Selected;
        p->setPen(Qt::NoPen);
        p->setBrush(selected ? QColor("#2f6fed") :
            option.state & QStyle::State_MouseOver ? QColor("#1b3b69") : QColor("#102a56"));
        p->drawRoundedRect(row, 8, 8);
        const QColor ink(selected ? "#ffffff" : "#d1ddef");
        p->setPen(QPen(ink, 1.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p->setBrush(Qt::NoBrush);
        p->save(); p->translate(row.left() + 14, row.center().y() - 9);
        switch (index.row()) {
        case 0:
            p->drawRoundedRect(QRectF(0, 1, 18, 13), 2, 2);
            p->drawLine(5, 18, 13, 18); p->drawLine(9, 14, 9, 18); break;
        case 1:
            for (int i = 0; i < 3; ++i) p->drawRoundedRect(QRectF(i*7, 11-i*4, 4, 7+i*4), 1, 1); break;
        case 2:
            p->drawRoundedRect(QRectF(2, 0, 14, 18), 2, 2);
            p->drawLine(6, 5, 12, 5); p->drawLine(6, 9, 12, 9); p->drawLine(7, 18, 7, 14); break;
        case 3: {
            QPolygonF bolt{QPointF(11,0),QPointF(3,10),QPointF(9,10),QPointF(7,19),QPointF(16,7),QPointF(10,7)};
            p->drawPolygon(bolt); break;
        }
        case 4:
            p->drawEllipse(QRectF(5,0,8,8)); p->drawArc(QRectF(1,11,16,14),0,180*16); break;
        case 5:
            p->drawRoundedRect(QRectF(2,0,14,18),2,2);
            for (int y : {5,9,13}) p->drawLine(6,y,12,y); break;
        case 6: {
            QPainterPath bubble; bubble.moveTo(3,0); bubble.lineTo(15,0); bubble.quadTo(18,0,18,3);
            bubble.lineTo(18,11); bubble.quadTo(18,14,15,14); bubble.lineTo(8,14);
            bubble.lineTo(3,18); bubble.lineTo(3,14); bubble.quadTo(0,14,0,11);
            bubble.lineTo(0,3); bubble.quadTo(0,0,3,0); p->drawPath(bubble);
            for (int x : {5,9,13}) p->drawPoint(x,7); break;
        }
        default:
            p->drawEllipse(QRectF(3,3,12,12)); p->drawEllipse(QRectF(7,7,4,4));
            for (int i=0;i<8;++i) { p->save(); p->translate(9,9); p->rotate(i*45); p->drawLine(0,-7,0,-9); p->restore(); }
        }
        p->restore();
        QFont font = option.font; font.setPixelSize(15); font.setWeight(selected ? QFont::DemiBold : QFont::Normal);
        p->setFont(font); p->setPen(ink);
        p->drawText(row.adjusted(46,0,-8,0), Qt::AlignVCenter | Qt::AlignLeft, index.data().toString());
        if (option.state & QStyle::State_HasFocus) {
            p->setPen(QPen(QColor("#a9c9ff"),1,Qt::DotLine)); p->drawRoundedRect(row.adjusted(1,1,-1,-1),8,8);
        }
        p->restore();
    }
};

void updateNavigationGroups(QListWidget *navigation)
{
    const QStringList categories{QStringLiteral("总览"), QStringLiteral("总览"),
        QStringLiteral("资产管理"), QStringLiteral("资产管理"), QStringLiteral("业务服务"),
        QStringLiteral("业务服务"), QStringLiteral("业务服务"), QStringLiteral("系统管理")};
    QString previous;
    for (int i = 0; i < navigation->count(); ++i) {
        auto *entry = navigation->item(i);
        entry->setData(navigationGroupRole, !entry->isHidden() && categories[i] != previous ? categories[i] : QString());
        entry->setData(Qt::AccessibleDescriptionRole, categories[i] + QStringLiteral("，") + entry->text());
        if (!entry->isHidden()) previous = categories[i];
    }
    navigation->doItemsLayout();
}

QFrame *panel(QWidget *parent)
{
    auto *frame = new QFrame(parent);
    frame->setObjectName(QStringLiteral("panel"));
    return frame;
}

QWidget *chartCard(const QString &title, QWidget *chart, QWidget *parent, const QString &note = {})
{
    auto *card = panel(parent);
    auto *layout = new QVBoxLayout(card);
    layout->setContentsMargins(20,18,20,16); layout->setSpacing(8);
    layout->addWidget(heading(title,card));
    if (!note.isEmpty()) {
        auto *caption = heading(note,card,"muted"); caption->setWordWrap(true); layout->addWidget(caption);
    }
    chart->setAccessibleName(title);
    layout->addWidget(chart,1);
    return card;
}

QWidget *scrollAnalysis(QWidget *page, const QString &name)
{
    auto *scroll = new QScrollArea;
    scroll->setObjectName(name);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto palette = page->palette();
    palette.setColor(QPalette::Window,QColor("#f3f6fb"));
    page->setPalette(palette); page->setAutoFillBackground(true);
    scroll->viewport()->setPalette(palette);
    scroll->setWidget(page);
    return scroll;
}

class MetricActionCard final : public QFrame {
public:
    using QFrame::QFrame;
    QLabel *value = nullptr;
    std::function<void()> action;
    void setActionPermitted(bool permitted) {
        permitted_ = permitted;
        setProperty("interactive", permitted);
        setCursor(permitted ? Qt::PointingHandCursor : Qt::ArrowCursor);
        setFocusPolicy(permitted ? Qt::StrongFocus : Qt::NoFocus);
        style()->unpolish(this); style()->polish(this); update();
    }
protected:
    void mousePressEvent(QMouseEvent *event) override {
        pressed_ = event->button()==Qt::LeftButton;
        if (pressed_) { setFocus(Qt::MouseFocusReason); event->accept(); }
        else QFrame::mousePressEvent(event);
    }
    void mouseReleaseEvent(QMouseEvent *event) override {
        const bool activate = pressed_ && event->button()==Qt::LeftButton && rect().contains(event->position().toPoint());
        pressed_ = false;
        if (activate) { trigger(); event->accept(); }
        else QFrame::mouseReleaseEvent(event);
    }
    void keyPressEvent(QKeyEvent *event) override {
        if (event->key()==Qt::Key_Return || event->key()==Qt::Key_Enter || event->key()==Qt::Key_Space) {
            if (!event->isAutoRepeat()) trigger(); event->accept();
        } else QFrame::keyPressEvent(event);
    }
private:
    void trigger() {
        if (permitted_ && action && value && value->text().contains(QRegularExpression(QStringLiteral("[0-9]")))) action();
    }
    bool permitted_ = true;
    bool pressed_ = false;
};

QWidget *metricCard(const QString &title,
                    const QString &accent,
                    QLabel **valueLabel,
                    QWidget *parent)
{
    auto *card = new MetricActionCard(parent);
    card->setObjectName(QStringLiteral("panel"));
    card->setAccessibleName(title);
    auto *layout = new QVBoxLayout(card);
    layout->setContentsMargins(20, 18, 20, 18);
    layout->setSpacing(8);
    auto *bar = new QFrame(card);
    bar->setFixedSize(36, 4);
    bar->setStyleSheet(QStringLiteral("background:%1;border-radius:2px;").arg(accent));
    layout->addWidget(bar, 0, Qt::AlignLeft);
    auto *titleLabel = new QLabel(title, card);
    titleLabel->setProperty("role", "muted");
    layout->addWidget(titleLabel);
    *valueLabel = new AnimatedMetricLabel(QStringLiteral("--"), card);
    card->value = *valueLabel;
    (*valueLabel)->setProperty("role", "metric");
    layout->addWidget(*valueLabel);
    return card;
}

QTableWidgetItem *item(const QString &text)
{
    auto *result = new QTableWidgetItem(text);
    result->setFlags(result->flags() & ~Qt::ItemIsEditable);
    return result;
}

QTableWidgetItem *numberItem(qint64 value)
{
    auto *result = item(QString::number(value));
    result->setData(Qt::UserRole, value);
    result->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    return result;
}

QString userStatusText(const QString &status)
{
    return status == QStringLiteral("ACTIVE") ? QStringLiteral("正常")
                                               : QStringLiteral("已冻结");
}

QString adminStatusText(const QString &status)
{
    return status == QStringLiteral("ACTIVE") ? QStringLiteral("启用")
                                               : QStringLiteral("停用");
}

QString adminRoleText(const QString &role)
{
    if (role == QStringLiteral("SYS_ADMIN")) return QStringLiteral("系统管理员");
    if (role == QStringLiteral("STATION_ADMIN")) return QStringLiteral("站点管理员");
    return QStringLiteral("用户管理员");
}

QString stationStatusText(const QString &status)
{
    return status == QStringLiteral("ACTIVE") ? QStringLiteral("启用")
                                               : QStringLiteral("停用");
}

QString pileStatusText(const QString &status)
{
    if (status == QStringLiteral("IDLE")) return QStringLiteral("空闲");
    if (status == QStringLiteral("RESERVED")) return QStringLiteral("已预约");
    if (status == QStringLiteral("CHARGING")) return QStringLiteral("充电中");
    if (status == QStringLiteral("FAULT")) return QStringLiteral("故障");
    return QStringLiteral("离线");
}

QString orderStatusText(const QString &status)
{
    if (status == QStringLiteral("RESERVED")) return QStringLiteral("已预约");
    if (status == QStringLiteral("CHARGING")) return QStringLiteral("充电中");
    if (status == QStringLiteral("PENDING_PAYMENT")) return QStringLiteral("待支付");
    if (status == QStringLiteral("COMPLETED")) return QStringLiteral("已完成");
    return QStringLiteral("已取消");
}

void colorStatus(QTableWidgetItem *tableItem, const QString &status)
{
    QColor color("#64748b");
    if (status=="ACTIVE" || status=="IDLE" || status=="COMPLETED") color=QColor("#187c6b");
    else if (status=="CHARGING") color=QColor("#245fc3");
    else if (status=="RESERVED") color=QColor("#7463c7");
    else if (status=="PENDING_PAYMENT" || status=="OFFLINE") color=QColor("#a66a12");
    else if (status=="FAULT" || status=="FROZEN") color=QColor("#bd3f48");
    tableItem->setForeground(color);
}

void updateFilterButton(QPushButton *button, int count)
{
    const QString title = button->property("filterTitle").toString();
    button->setText(count > 0 ? QStringLiteral("%1（%2） ▾").arg(title).arg(count)
                              : QStringLiteral("%1 ▾").arg(title));
}

void installMultiSelectMenu(QPushButton *button,
                            QComboBox *source,
                            const std::function<bool(const QVariant &)> &isSelected,
                            const std::function<void(const QVariant &, bool)> &toggle,
                            const std::function<void()> &clear,
                            const std::function<void()> &apply = {})
{
    QObject::connect(button, &QPushButton::clicked, button, [button, source, isSelected, toggle, clear, apply] {
        QDialog dialog(button);
        dialog.setWindowFlags(Qt::Popup | Qt::FramelessWindowHint);
        dialog.setAttribute(Qt::WA_DeleteOnClose, false);
        dialog.setObjectName(QStringLiteral("managementFilterPopup"));
        const QRect available = button->screen()->availableGeometry().adjusted(8, 8, -8, -8);
        const int popupWidth = qMin(qMax(280, button->width()+60), qMin(380, available.width()));
        dialog.setFixedWidth(popupWidth);
        auto *layout = new QVBoxLayout(&dialog);
        layout->setContentsMargins(14, 12, 14, 12);
        layout->setSpacing(8);
        auto *title = new QLabel(button->property("filterTitle").toString(), &dialog);
        title->setStyleSheet(QStringLiteral("font-weight:600;color:#243b64;"));
        layout->addWidget(title);
        auto *all = new QCheckBox(QStringLiteral("全选"), &dialog);
        all->setObjectName(QStringLiteral("filterSelectAll"));
        layout->addWidget(all);
        auto *scroll = new QScrollArea(&dialog);
        scroll->setObjectName(QStringLiteral("filterOptionsScroll"));
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setWidgetResizable(true);
        scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        auto *options = new QWidget(scroll);
        auto *optionsLayout = new QVBoxLayout(options);
        optionsLayout->setContentsMargins(0, 0, 4, 0);
        optionsLayout->setSpacing(2);
        QVector<QCheckBox *> boxes;
        for (int i = 1; i < source->count(); ++i) {
            const QString fullName = source->itemText(i);
            auto *box = new QCheckBox(options);
            box->setMinimumHeight(30);
            box->setText(box->fontMetrics().elidedText(fullName, Qt::ElideRight, popupWidth-68)
                             .replace('&', QStringLiteral("&&")));
            box->setToolTip(fullName);
            box->setAccessibleName(fullName);
            box->setChecked(isSelected(source->itemData(i)));
            boxes.append(box);
            optionsLayout->addWidget(box);
        }
        optionsLayout->addStretch();
        scroll->setWidget(options);
        auto surface = dialog.palette();
        surface.setColor(QPalette::Window, QColor("#f3f6fb"));
        scroll->viewport()->setPalette(surface);
        scroll->viewport()->setAutoFillBackground(true);
        options->setPalette(surface);
        options->setAutoFillBackground(true);
        scroll->setFixedHeight(qMin(qMax(36, boxes.size()*32), qMax(36, qMin(380, available.height()-160))));
        layout->addWidget(scroll);
        auto syncAll = [all, &boxes] {
            bool every = !boxes.isEmpty();
            for (auto *box : boxes) every = every && box->isChecked();
            QSignalBlocker blocker(all); all->setChecked(every);
        };
        for (auto *box : boxes) QObject::connect(box, &QCheckBox::toggled, &dialog, syncAll);
        QObject::connect(all, &QCheckBox::toggled, &dialog, [&boxes](bool checked){ for (auto *box : boxes) box->setChecked(checked); });
        syncAll();
        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok, &dialog);
        auto *confirm = buttons->button(QDialogButtonBox::Ok);
        confirm->setText(QStringLiteral("确认"));
        confirm->setIcon(QIcon());
        confirm->setProperty("primary", true);
        confirm->style()->unpolish(confirm);
        confirm->style()->polish(confirm);
        layout->addWidget(buttons);
        QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        dialog.adjustSize();
        QPoint popupPos = button->mapToGlobal(QPoint(0, button->height()+4));
        if (popupPos.y()+dialog.height() > available.bottom()+1)
            popupPos.setY(button->mapToGlobal(QPoint(0,0)).y()-dialog.height()-4);
        popupPos.setX(qBound(available.left(), popupPos.x(), qMax(available.left(),available.right()-dialog.width()+1)));
        popupPos.setY(qBound(available.top(), popupPos.y(), qMax(available.top(),available.bottom()-dialog.height()+1)));
        dialog.move(popupPos);
        if (dialog.exec() != QDialog::Accepted) return;
        clear();
        for (int i = 0; i < boxes.size(); ++i) if (boxes[i]->isChecked()) toggle(source->itemData(i + 1), true);
        if (apply) apply();
    });
}

class DetailsDialog final : public QDialog {
public:
    explicit DetailsDialog(QWidget *parent = nullptr) : QDialog(parent)
    {
        setWindowFlags(Qt::Popup | Qt::FramelessWindowHint);
        setAttribute(Qt::WA_DeleteOnClose, false);
    }

    void enableClickToClose()
    {
        installEventFilter(this);
    }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (event->type() == QEvent::WindowDeactivate && isVisible()) {
            reject();
            return true;
        }
        return QDialog::eventFilter(watched, event);
    }
};

class StationRowDelegate final : public QStyledItemDelegate {
public:
    explicit StationRowDelegate(QObject *parent = nullptr) : QStyledItemDelegate(parent) {}

    void initStyleOption(QStyleOptionViewItem *option,
                         const QModelIndex &index) const override
    {
        QStyledItemDelegate::initStyleOption(option, index);
        // QSS uses pixel units while QFont::setPointSize() uses points.  Set
        // the rendered font explicitly in pixels so child rows are always
        // visibly smaller than station rows on every display scale.
        option->font.setPixelSize(index.parent().isValid() ? 14 : 16);
        option->font.setWeight(QFont::Normal);
    }

    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        QSize size = QStyledItemDelegate::sizeHint(option, index);
        // Parent station rows use the same comfortable height as the three
        // management tables; child pile rows remain slightly denser.
        size.setHeight(index.parent().isValid() ? 40 : 48);
        return size;
    }
};

}  // namespace

AdminWindow::AdminWindow(AdminFacade *facade,
                         bool tcpListening,
                         quint16 tcpPort,
                         bool sqliteRepository,
                         QWidget *parent)
    : QMainWindow(parent)
    , facade_(facade)
    , tcpListening_(tcpListening)
    , tcpPort_(tcpPort)
    , sqliteRepository_(sqliteRepository)
{
    setWindowTitle(QStringLiteral("悦充 · 充电运营管理平台"));
    const auto available = QGuiApplication::primaryScreen()->availableGeometry().size();
    resize(qMin(1380,available.width()-48), qMin(860,available.height()-48));
    setMinimumSize(1080, 700);
    rootStack_ = new QStackedWidget(this);
    rootStack_->addWidget(buildLoginPage());
    rootStack_->addWidget(buildApplicationPage());
    setCentralWidget(rootStack_);

    setStyleSheet(QStringLiteral(R"(
        QMainWindow, QStackedWidget, QDialog { background: #f3f6fb; }
        QWidget { color: #243044; font-family: "Noto Sans CJK SC", "Microsoft YaHei", sans-serif; font-size: 14px; }
        QFrame#panel { background: white; border: 1px solid #e2e8f1; border-radius: 12px; }
        QFrame#panel[interactive="true"]:hover, QFrame#panel[interactive="true"]:focus { border: 1px solid #85aaf1; background:#f8fbff; }
        QFrame#brandPanel { background: #1746a2; border: none; border-radius: 12px; }
        QFrame#loginCard { background: white; border: 1px solid #dfe7f2; border-radius: 20px; }
        QWidget#loginSurface, QWidget#loginSurface QLabel { background: white; }
        QLabel[role="loginBrandTitle"] { color: white; font-size: 46px; font-weight: 600; background: transparent; }
        QLabel[role="loginBrandCaption"] { color: #d6e4fc; font-size: 17px; background: transparent; }
        QLabel[role="loginTitle"] { color: #172b49; font-size: 30px; font-weight: 600; }
        QLabel[role="loginLabel"] { color: #344761; font-size: 15px; font-weight: 500; }
        QLineEdit#loginInput { background: #f8faff; border: 1px solid #d8e2ef; border-radius: 10px; padding: 10px 14px; font-size: 16px; color: #173653; }
        QLineEdit#loginInput:hover { border-color: #9cb9de; }
        QLineEdit#loginInput:focus { border: 2px solid #2f6fed; padding: 9px 13px; background: white; }
        QToolButton#loginPasswordToggle { background: transparent; color: #2459a5; font-size: 13px; border: none; padding: 0; }
        QToolButton#loginPasswordToggle:hover, QToolButton#loginPasswordToggle:focus { background: #e7effc; }
        QLabel#loginError { color: transparent; background: white; border: none; border-radius: 7px; padding: 7px 10px; font-size: 13px; }
        QLabel#loginError[hasError="true"] { color: #b42318; background: #fff3f1; border: 1px solid #f5c2bd; }
        QPushButton#loginSubmit { color: white; background: #2f6fed; border: none; border-radius: 10px; padding: 10px 16px; font-size: 16px; font-weight: 600; }
        QPushButton#loginSubmit:hover { background: #2459c5; }
        QPushButton#loginSubmit:pressed { background: #19449e; }
        QLabel[role="hero"] { color: white; font-size: 27px; font-weight: 600; }
        QLabel[role="heroSub"] { color: #cdddff; font-size: 14px; }
        QLabel[role="title"] { font-size: 23px; font-weight: 600; }
        QLabel[role="sectionTitle"] { font-size: 17px; font-weight: 600; }
        QLabel[role="muted"] { color: #64748b; }
        QLabel[role="metric"] { font-size: 25px; font-weight: 600; color: #172033; }
        QLabel[role="error"] { color: #c33838; }
        QLabel[role="badgeOk"] { color: #157347; background: #e7f7ed; padding: 6px 10px; border-radius: 12px; }
        QLabel[role="badgeBad"] { color: #a61b1b; background: #ffe9e9; padding: 6px 10px; border-radius: 12px; }
        QLineEdit, QComboBox, QSpinBox, QDoubleSpinBox, QDateTimeEdit { background: white; border: 1px solid #d8dfeb; border-radius: 6px; padding: 6px 9px; min-height: 20px; }
        QDialog#adminAccountDialog { background: #f3f6fb; }
        QComboBox::drop-down { border: none; width: 26px; }
        QComboBox QAbstractItemView { background: white; color: #243044; border: 1px solid #dfe6f0; selection-background-color: #e8f0ff; selection-color: #183b70; outline: none; padding: 4px; }
        QLineEdit:focus, QComboBox:focus, QSpinBox:focus, QDoubleSpinBox:focus { border: 1px solid #2f6fed; }
        QPushButton { background: #eef3f9; border: 1px solid transparent; border-radius: 6px; padding: 7px 13px; color: #244168; font-weight: 500; }
        QPushButton:hover { background: #dfe8f7; }
        QPushButton[primary="true"] { color: white; background: #2f6fed; }
        QPushButton[primary="true"]:hover { background: #2459c5; }
        QPushButton[danger="true"] { color: #a61b1b; background: #ffe9e9; }
        QToolButton { color: #244168; background: transparent; border: none; border-radius: 6px; padding: 6px; }
        QToolButton:hover:enabled { background: #dfe8f7; }
        QToolButton:disabled { color: #aeb8c7; }
        QFrame#navigationControls { background: #eef3f9; border: 1px solid #dfe5ef; border-radius: 8px; }
        QFrame#navigationControls QToolButton:hover:enabled { background: #dfe8f7; }
        QListWidget#navigation { background: #102a56; border: none; outline: none; padding: 0; }
        QListWidget#navigation QScrollBar:vertical { background: #102a56; width: 6px; margin: 0; }
        QListWidget#navigation QScrollBar::handle:vertical { background: #496487; border-radius: 3px; min-height: 28px; }
        QTableWidget, QTreeWidget { background: white; border: 1px solid #dfe6f0; border-radius: 10px; gridline-color: #edf1f7; selection-background-color: #e8f0ff; selection-color: #183b70; alternate-background-color: #fafbfd; font-size: 14px; outline: none; }
        QTableWidget::item, QTreeWidget::item { padding: 8px 10px; border-bottom: 1px solid #edf1f7; }
        QTableWidget::item:hover:!selected, QTreeWidget::item:hover:!selected { background: #f0f5fc; }
        QTableWidget::item:selected, QTreeWidget::item:selected { background: #e8f0ff; color: #183b70; }
        QHeaderView::section { background: #f6f8fc; color: #52637c; border: none; border-bottom: 1px solid #dfe6f0; padding: 10px; font-size: 14px; font-weight: 600; }
        QTableCornerButton::section { background: #f6f8fc; border: none; }
        QPlainTextEdit { background: white; color: #243044; border: 1px solid #d8e1ee; border-radius: 8px; padding: 10px; selection-background-color: #dbe8ff; }
        QPlainTextEdit:focus { border: 1px solid #2f6fed; }
        QPushButton:disabled { color: #8996aa; background: #edf1f6; }
        QPushButton:pressed:enabled { background: #d2dff4; }
        QPushButton[primary="true"]:pressed { background: #19449e; }
        QPushButton:focus { border: 1px solid #7da6f4; }
        QMenu { background: white; border: 1px solid #dfe6f0; padding: 5px; }
        QMenu::item { padding: 8px 22px; border-radius: 5px; }
        QMenu::item:selected { background: #e8f0ff; color: #183b70; }
        QScrollBar:vertical { background: #f4f6fa; width: 8px; margin: 0; }
        QScrollBar:horizontal { background: #f4f6fa; height: 8px; margin: 0; }
        QScrollBar::handle:vertical, QScrollBar::handle:horizontal { background: #c6d1df; border-radius: 4px; min-height: 28px; min-width: 28px; }
        QScrollBar::add-line, QScrollBar::sub-line { height: 0; width: 0; }
        QScrollBar::add-page, QScrollBar::sub-page { background: none; }
        QLabel#adminTicketNotice { color: #52637c; padding: 8px 0; }
        QLabel#adminTicketCount { color: #64748b; }
        QListWidget#adminTicketList { background: white; border: none; outline: none; }
        QListWidget#adminTicketList::item { padding: 14px 12px; margin: 2px 0; border-bottom: 1px solid #edf1f7; border-radius: 8px; }
        QListWidget#adminTicketList::item:selected { background: #e8f0ff; color: #183b70; }
        QListWidget#adminTicketList::item:hover:!selected { background: #f4f7fc; }
        QPlainTextEdit#adminTicketSummary { background: #f8faff; border: none; }
        QWidget#managementPage QLineEdit,
        QWidget#managementPage QComboBox,
        QWidget#managementPage QDateEdit,
        QWidget#managementPage QSpinBox,
        QWidget#managementPage QDoubleSpinBox {
            font-size: 15px;
            min-height: 26px;
            padding: 6px 11px;
        }
        QWidget#managementPage QComboBox::drop-down { width: 28px; }
        QWidget#managementPage QPushButton {
            font-size: 15px;
            min-height: 26px;
            padding: 7px 15px;
        }
        QLabel[role="pageTitle"] { font-size: 26px; font-weight: 600; }
    )"));
}

void AdminWindow::showDetails(const QString &title, const QString &content)
{
    DetailsDialog dialog(this);
    dialog.setWindowTitle(title);
    dialog.setModal(true);
    auto *layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(14, 14, 14, 14);
    layout->setSpacing(12);
    layout->addWidget(heading(title, &dialog, "title"));
    auto *scroll = new QScrollArea(&dialog);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto *card = panel(scroll);
    card->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    auto *form = new QFormLayout(card);
    form->setContentsMargins(20, 18, 20, 18);
    form->setHorizontalSpacing(16);
    form->setVerticalSpacing(10);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    const QStringList lines = content.split('\n', Qt::SkipEmptyParts);
    QTableWidget *detailTable = nullptr;
    for (const QString &line : lines) {
        if (line.startsWith(QStringLiteral("• "))) {
            if (detailTable == nullptr) {
                detailTable = new QTableWidget(card);
                prepareTable(detailTable, {QStringLiteral("电桩编号"), QStringLiteral("类型"),
                                           QStringLiteral("功率"), QStringLiteral("状态")});
                detailTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
                detailTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
                detailTable->setSelectionMode(QAbstractItemView::NoSelection);
                detailTable->setFocusPolicy(Qt::NoFocus);
                detailTable->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
                form->addRow(detailTable);
            }
            const QStringList values = line.mid(2).split('\t');
            const int row = detailTable->rowCount();
            detailTable->insertRow(row);
            for (int column = 0; column < 4; ++column) {
                detailTable->setItem(row, column, item(values.value(column, QStringLiteral("—"))));
            }
            continue;
        }
        const int sep = line.indexOf(QChar(0xFF1A));
        if (sep > 0) {
            auto *value = new QLabel(line.mid(sep + 1).trimmed(), card);
            value->setTextFormat(Qt::PlainText);
            value->setTextInteractionFlags(Qt::TextSelectableByMouse);
            value->setWordWrap(true);
            value->setMinimumWidth(120);
            value->setMaximumWidth(460);
            value->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
            auto *label = new QLabel(line.left(sep), card);
            label->setStyleSheet(QStringLiteral("color:#667085;font-weight:600;"));
            label->setMinimumWidth(78);
            form->addRow(label, value);
        } else {
            auto *section = new QLabel(line, card);
            section->setStyleSheet(QStringLiteral("font-weight:600;color:#243b64;padding-top:8px;"));
            form->addRow(section);
        }
    }
    if (detailTable != nullptr) {
        detailTable->resizeColumnsToContents();
        int tableWidth = detailTable->verticalHeader()->width() + 16;
        for (int column = 0; column < detailTable->columnCount(); ++column) {
            tableWidth += detailTable->columnWidth(column);
        }
        detailTable->setMinimumWidth(qBound(420, tableWidth, 680));
        detailTable->setMinimumHeight(qMin(300, 42 + detailTable->rowCount() * 36));
        detailTable->setMaximumHeight(qMin(300, 42 + detailTable->rowCount() * 36));
    }
    scroll->setWidget(card);
    layout->addWidget(scroll, 1);
    card->adjustSize();
    const QSize available = QGuiApplication::primaryScreen()->availableGeometry().size();
    const int preferredWidth = card->sizeHint().width() + 28;
    const int preferredHeight = card->sizeHint().height() + 82;
    const int width = qBound(360, preferredWidth, qMin(780, available.width() - 80));
    const int height = qBound(220, preferredHeight, qMin(700, available.height() - 100));
    dialog.resize(width, height);
    dialog.enableClickToClose();
    QEventLoop loop;
    connect(&dialog, &QDialog::finished, &loop, &QEventLoop::quit);
    dialog.show();
    loop.exec();
}

QWidget *AdminWindow::buildLoginPage()
{
    auto *page = new LoginBackdrop(this);
    page->setObjectName(QStringLiteral("loginPage"));
    auto *outer = new QVBoxLayout(page);
    outer->setContentsMargins(32, 32, 32, 32);
    auto *card = new QFrame(page);
    card->setObjectName(QStringLiteral("loginCard"));
    card->setFixedSize(1000, 580);
    auto *cardLayout = new QHBoxLayout(card);
    cardLayout->setContentsMargins(1, 1, 20, 1);
    cardLayout->setSpacing(0);
    auto *brand = new LoginBrandPanel(card);
    brand->setObjectName("loginBrandPanel");
    brand->setFixedWidth(420);
    auto *brandLayout = new QVBoxLayout(brand);
    brandLayout->setContentsMargins(36, 48, 36, 48);
    brandLayout->setSpacing(14);
    brandLayout->addStretch();
    brandLayout->addWidget(new LoginLogo(brand), 0, Qt::AlignHCenter);
    brandLayout->addSpacing(12);
    auto *brandTitle = heading(QStringLiteral("悦充"), brand, "loginBrandTitle");
    brandTitle->setAlignment(Qt::AlignCenter);
    brandLayout->addWidget(brandTitle);
    auto *caption = heading(QStringLiteral("充电运营管理平台"), brand, "loginBrandCaption");
    caption->setAlignment(Qt::AlignCenter);
    brandLayout->addWidget(caption);
    brandLayout->addStretch();
    cardLayout->addWidget(brand);

    auto *surface = new QWidget(card);
    surface->setObjectName(QStringLiteral("loginSurface"));
    surface->setAutoFillBackground(true);
    auto palette = surface->palette();
    palette.setColor(QPalette::Window, Qt::white);
    surface->setPalette(palette);
    cardLayout->addWidget(surface, 1);
    auto *formLayout = new QVBoxLayout(surface);
    formLayout->setContentsMargins(58, 48, 58, 48);
    formLayout->setSpacing(8);
    formLayout->addStretch();
    formLayout->addWidget(heading(QStringLiteral("管理员登录"), surface, "loginTitle"));
    formLayout->addSpacing(22);
    auto *usernameLabel = heading(QStringLiteral("账号"), surface, "loginLabel");
    formLayout->addWidget(usernameLabel);
    usernameEdit_ = new QLineEdit(surface);
    usernameEdit_->setObjectName(QStringLiteral("loginInput"));
    usernameEdit_->setFixedHeight(52);
    usernameEdit_->setPlaceholderText(QStringLiteral("请输入管理员账号"));
    usernameEdit_->setAccessibleName(QStringLiteral("管理员账号"));
    usernameLabel->setBuddy(usernameEdit_);
    formLayout->addWidget(usernameEdit_);
    formLayout->addSpacing(12);
    auto *passwordLabel = heading(QStringLiteral("密码"), surface, "loginLabel");
    formLayout->addWidget(passwordLabel);
    passwordEdit_ = new LoginPasswordEdit(surface);
    passwordEdit_->setObjectName(QStringLiteral("loginInput"));
    passwordEdit_->setFixedHeight(52);
    passwordEdit_->setPlaceholderText(QStringLiteral("请输入密码"));
    passwordEdit_->setAccessibleName(QStringLiteral("密码"));
    passwordLabel->setBuddy(passwordEdit_);
    formLayout->addWidget(passwordEdit_);
    loginError_ = new QLabel(surface);
    loginError_->setObjectName(QStringLiteral("loginError"));
    loginError_->setTextFormat(Qt::PlainText);
    loginError_->setFixedHeight(34);
    loginError_->setWordWrap(true);
    loginError_->setProperty("hasError", false);
    formLayout->addWidget(loginError_);
    auto *loginButton = new QPushButton(QStringLiteral("登录管理后台"), surface);
    loginButton->setObjectName(QStringLiteral("loginSubmit"));
    loginButton->setFixedHeight(52);
    loginButton->setCursor(Qt::PointingHandCursor);
    connect(loginButton, &QPushButton::clicked, this, &AdminWindow::attemptLogin);
    connect(passwordEdit_, &QLineEdit::returnPressed, this, &AdminWindow::attemptLogin);
    connect(usernameEdit_, &QLineEdit::returnPressed, passwordEdit_, [this] { passwordEdit_->setFocus(); });
    formLayout->addWidget(loginButton);
    formLayout->addStretch();
    QWidget::setTabOrder(usernameEdit_, passwordEdit_);
    outer->addWidget(card, 1, Qt::AlignCenter);
    return page;
}

QWidget *AdminWindow::buildApplicationPage()
{
    auto *page = new QWidget(this);
    auto *layout = new QHBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    auto *sidebar = new QFrame(page);
    sidebar->setFixedWidth(228);
    sidebar->setObjectName("adminSidebar");
    sidebar->setStyleSheet(QStringLiteral("QFrame#adminSidebar { background:#102a56; border:none; }"));
    auto *sidebarLayout = new QVBoxLayout(sidebar);
    sidebarLayout->setContentsMargins(12, 22, 12, 16);
    sidebarLayout->setSpacing(16);
    auto *logo = new QLabel(QStringLiteral("ϟ  悦充管理端"), sidebar);
    logo->setStyleSheet(QStringLiteral("color:white;font-size:21px;font-weight:600;padding:4px 8px;"));
    logo->setMinimumHeight(40);
    sidebarLayout->addWidget(logo);
    navigation_ = new QListWidget(sidebar);
    navigation_->setObjectName(QStringLiteral("navigation"));
    navigation_->addItems({QStringLiteral("运营监控"), QStringLiteral("营收统计"),
                           QStringLiteral("充电站管理"), QStringLiteral("充电桩管理"),
                           QStringLiteral("用户管理"), QStringLiteral("订单管理"),
                           QStringLiteral("客服工单"), QStringLiteral("管理员管理")});
    navigation_->setItemDelegate(new NavigationDelegate(navigation_));
    navigation_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    navigation_->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    navigation_->setFrameShape(QFrame::NoFrame);
    navigation_->setMouseTracking(true);
    auto navigationPalette = navigation_->palette();
    navigationPalette.setColor(QPalette::Base, QColor("#102a56"));
    navigationPalette.setColor(QPalette::Window, QColor("#102a56"));
    navigation_->setPalette(navigationPalette);
    navigation_->viewport()->setPalette(navigationPalette);
    navigation_->viewport()->setAutoFillBackground(true);
    updateNavigationGroups(navigation_);
    sidebarLayout->addWidget(navigation_, 1);
    auto *accountPanel = new QFrame(sidebar);
    accountPanel->setStyleSheet(QStringLiteral(
        "QFrame { background:#193a70; border-radius:8px; }"
        "QLabel { color:#dce7f8; font-size:15px; }"
        "QPushButton { color:#dce7f8; background:#28518b; border-radius:6px;"
        " padding:7px 10px; font-size:15px; }"
        "QPushButton:hover { background:#3565a4; }"));
    auto *accountLayout = new QVBoxLayout(accountPanel);
    accountLayout->setContentsMargins(10, 10, 10, 10);
    accountLayout->setSpacing(8);
    accountIdentity_ = new QLabel(QStringLiteral("未登录"), accountPanel);
    accountIdentity_->setWordWrap(true);
    accountLayout->addWidget(accountIdentity_);
    changePasswordButton_ = new QPushButton(QStringLiteral("修改密码"), accountPanel);
    changePasswordButton_->setObjectName(QStringLiteral("adminChangePassword"));
    connect(changePasswordButton_, &QPushButton::clicked, this, [this] {
        showChangePasswordDialog(false);
    });
    accountLayout->addWidget(changePasswordButton_);
    auto *logoutButton = new QPushButton(QStringLiteral("退出登录"), accountPanel);
    connect(logoutButton, &QPushButton::clicked, this, [this] {
        if (facade_ != nullptr) facade_->logout();
        currentAdminId_ = 0;
        currentAdminRole_.clear();
        currentAdminAccountsAvailable_ = false;
        supportTicketsPage_->clear();
        rootStack_->setCurrentIndex(0);
        passwordEdit_->clear();
        backHistory_.clear();
        forwardHistory_.clear();
        updateNavigationButtons();
    });
    accountLayout->addWidget(logoutButton);
    sidebarLayout->addWidget(accountPanel);
    layout->addWidget(sidebar);

    auto *mainArea = new QWidget(page);
    auto *mainLayout = new QVBoxLayout(mainArea);
    mainLayout->setContentsMargins(26, 18, 26, 24);
    mainLayout->setSpacing(16);
    auto *topBar = new QHBoxLayout;
    pageTitle_ = heading(QStringLiteral("运营监控"), mainArea, "title");
    pageTitle_->setProperty("role", "pageTitle");
    topBar->addWidget(pageTitle_);
    topBar->addStretch();
    backButton_ = new QToolButton(mainArea);
    backButton_->setObjectName(QStringLiteral("adminPageBack"));
    backButton_->setIcon(style()->standardIcon(QStyle::SP_ArrowBack));
    backButton_->setIconSize(QSize(20, 20));
    backButton_->setToolTip(QStringLiteral("后退"));
    backButton_->setAccessibleName(QStringLiteral("后退"));
    backButton_->setAutoRaise(true);
    backButton_->setFixedSize(36, 36);
    refreshButton_ = new QToolButton(mainArea);
    refreshButton_->setObjectName(QStringLiteral("adminPageRefresh"));
    refreshButton_->setIcon(style()->standardIcon(QStyle::SP_BrowserReload));
    refreshButton_->setIconSize(QSize(20, 20));
    refreshButton_->setToolTip(QStringLiteral("刷新当前页面"));
    refreshButton_->setAccessibleName(QStringLiteral("刷新当前页面"));
    refreshButton_->setAutoRaise(true);
    refreshButton_->setFixedSize(36, 36);
    forwardButton_ = new QToolButton(mainArea);
    forwardButton_->setObjectName(QStringLiteral("adminPageForward"));
    forwardButton_->setIcon(style()->standardIcon(QStyle::SP_ArrowForward));
    forwardButton_->setIconSize(QSize(20, 20));
    forwardButton_->setToolTip(QStringLiteral("前进"));
    forwardButton_->setAccessibleName(QStringLiteral("前进"));
    forwardButton_->setAutoRaise(true);
    forwardButton_->setFixedSize(36, 36);
    auto *navigationControls = new QFrame(mainArea);
    navigationControls->setObjectName(QStringLiteral("navigationControls"));
    auto *navigationControlsLayout = new QHBoxLayout(navigationControls);
    navigationControlsLayout->setContentsMargins(2, 2, 2, 2);
    navigationControlsLayout->setSpacing(0);
    navigationControlsLayout->addWidget(backButton_);
    navigationControlsLayout->addWidget(refreshButton_);
    navigationControlsLayout->addWidget(forwardButton_);
    topBar->addWidget(navigationControls);
    connect(backButton_, &QToolButton::clicked, this, &AdminWindow::navigateBack);
    connect(refreshButton_, &QToolButton::clicked, this, &AdminWindow::refreshCurrentPage);
    connect(forwardButton_, &QToolButton::clicked, this, &AdminWindow::navigateForward);
    auto *backShortcut = new QShortcut(QKeySequence(Qt::ALT | Qt::Key_Left), this);
    auto *forwardShortcut = new QShortcut(QKeySequence(Qt::ALT | Qt::Key_Right), this);
    connect(backShortcut, &QShortcut::activated, this, &AdminWindow::navigateBack);
    connect(forwardShortcut, &QShortcut::activated, this, &AdminWindow::navigateForward);
    mainLayout->addLayout(topBar);
    contentStack_ = new QStackedWidget(mainArea);
    contentStack_->addWidget(buildOperationsPage());
    contentStack_->addWidget(buildDashboardPage());
    contentStack_->addWidget(buildStationsPage());
    contentStack_->addWidget(buildPilesPage());
    contentStack_->addWidget(buildUsersPage());
    contentStack_->addWidget(buildOrdersPage());
    supportTicketsPage_ = new SupportTicketsPage(facade_, contentStack_);
    connect(supportTicketsPage_, &SupportTicketsPage::locatePileRequested,
            this, &AdminWindow::navigateToTicketPile);
    contentStack_->addWidget(supportTicketsPage_);
    contentStack_->addWidget(buildAdminsPage());
    wireAnalysisActions();
    refreshFeedback_ = new PageRefreshFeedback(contentStack_);
    mainLayout->addWidget(contentStack_, 1);
    layout->addWidget(mainArea, 1);
    connect(navigation_, &QListWidget::currentRowChanged,
            this, &AdminWindow::selectPage);
    navigation_->setCurrentRow(0);
    historyReady_ = true;
    updateNavigationButtons();
    return page;
}

QWidget *AdminWindow::buildDashboardPage()
{
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(16);
    auto *rangeRow = new QHBoxLayout;
    dashboardPeriod_ = new QLabel(page);
    dashboardPeriod_->setObjectName(QStringLiteral("revenueDateRange"));
    dashboardPeriod_->setStyleSheet(QStringLiteral("color:#334d70; font-size:20px; font-weight:500;"));
    dashboardPeriod_->setToolTip(QStringLiteral("统计日期范围，包含起止两天"));
    rangeRow->addWidget(dashboardPeriod_, 0, Qt::AlignVCenter);
    rangeRow->addStretch();
    dashboardCustomRange_ = new QWidget(page);
    dashboardCustomRange_->setObjectName(QStringLiteral("revenueCustomRange"));
    auto *customRow = new QHBoxLayout(dashboardCustomRange_);
    customRow->setContentsMargins(0, 0, 0, 0);
    customRow->addStretch();
    dashboardCustomRange_->hide();
    dashboardDays_ = new AdminComboBox(page);
    dashboardDays_->addItem(QStringLiteral("近 7 日"), 7);
    dashboardDays_->addItem(QStringLiteral("近 30 日"), 30);
    dashboardDays_->addItem(QStringLiteral("近 60 日"), 60);
    dashboardDays_->addItem(QStringLiteral("近 90 日"), 90);
    dashboardDays_->addItem(QStringLiteral("自定义"), -1);
    dashboardStartDate_ = new QDateEdit(QDateTime::currentDateTimeUtc().toTimeZone(QTimeZone("Asia/Shanghai")).date().addDays(-6), page);
    dashboardStartDate_->setCalendarPopup(true);
    dashboardStartDate_->setDisplayFormat(QStringLiteral("yyyy-MM-dd"));
    dashboardEndDate_ = new QDateEdit(QDateTime::currentDateTimeUtc().toTimeZone(QTimeZone("Asia/Shanghai")).date(), page);
    dashboardEndDate_->setCalendarPopup(true);
    dashboardEndDate_->setDisplayFormat(QStringLiteral("yyyy-MM-dd"));
    dashboardApplyButton_ = new QPushButton(QStringLiteral("应用"), page);
    dashboardStartLabel_ = new QLabel(QStringLiteral("起始"), page);
    dashboardEndLabel_ = new QLabel(QStringLiteral("终止"), page);
    for (QWidget *control : {static_cast<QWidget *>(dashboardStartLabel_),
                             static_cast<QWidget *>(dashboardStartDate_),
                             static_cast<QWidget *>(dashboardEndLabel_),
                             static_cast<QWidget *>(dashboardEndDate_),
                             static_cast<QWidget *>(dashboardApplyButton_)}) control->hide();
    connect(dashboardDays_, &QComboBox::currentIndexChanged,
            this, [this] {
                const bool custom = dashboardDays_->currentData().toInt() < 0;
                dashboardCustomRange_->setVisible(custom);
                dashboardStartLabel_->setVisible(custom);
                dashboardStartDate_->setVisible(custom);
                dashboardEndLabel_->setVisible(custom);
                dashboardEndDate_->setVisible(custom);
                dashboardApplyButton_->setVisible(custom);
                if (!custom) {
                    refreshDashboard();
                    playAnalysisIntro(1);
                }
            });
    connect(dashboardApplyButton_, &QPushButton::clicked, this, [this] {
        refreshDashboard();
        playAnalysisIntro(1);
    });
    rangeRow->addWidget(dashboardDays_);
    customRow->addWidget(dashboardStartLabel_);
    customRow->addWidget(dashboardStartDate_);
    customRow->addWidget(dashboardEndLabel_);
    customRow->addWidget(dashboardEndDate_);
    customRow->addWidget(dashboardApplyButton_);
    layout->addLayout(rangeRow);
    layout->addWidget(dashboardCustomRange_);
    auto *metrics = new QGridLayout;
    metrics->setSpacing(14);
    metrics->addWidget(metricCard(QStringLiteral("今日营收"), QStringLiteral("#2f6fed"), &todayRevenue_, page), 0, 0);
    metrics->addWidget(metricCard(QStringLiteral("本月营收"), QStringLiteral("#13a06f"), &monthRevenue_, page), 0, 1);
    metrics->addWidget(metricCard(QStringLiteral("累计营收"), QStringLiteral("#f29d38"), &totalRevenue_, page), 0, 2);
    metrics->addWidget(metricCard(QStringLiteral("站点 / 电桩"), QStringLiteral("#875bd8"), &resourceCount_, page), 0, 3);
    metrics->addWidget(metricCard(QStringLiteral("区间实收"), "#2f6fed", &rangeRevenue_, page),1,0);
    metrics->addWidget(metricCard(QStringLiteral("已支付订单"), "#7463c7", &rangeOrders_, page),1,1);
    metrics->addWidget(metricCard(QStringLiteral("已支付订单电量"), "#159b8d", &rangeEnergy_, page),1,2);
    metrics->addWidget(metricCard(QStringLiteral("平均每单实收"), "#c98620", &rangeAverage_, page),1,3);
    rangeRevenue_->setObjectName("rangeReceivedRevenue");
    rangeOrders_->setObjectName("rangePaidOrders");
    rangeEnergy_->setObjectName("rangePaidEnergy");
    layout->addLayout(metrics);
    analysisSummary_ = heading({},page,"muted");
    analysisSummary_->setObjectName("dashboardAnalysisSummary");
    analysisSummary_->setWordWrap(true);
    layout->addWidget(analysisSummary_);
    analysisSummary_->hide();
    auto *charts = new QGridLayout;
    charts->setSpacing(16);
    charts->setColumnStretch(0,1); charts->setColumnStretch(1,1);
    revenueChart_ = new RevenueChart(page);
    charts->addWidget(chartCard(QStringLiteral("每日实收"),revenueChart_,page),0,0);
    stationRevenueChart_ = new AnalysisBarChart(page);
    stationRevenueChart_->setObjectName("stationRevenueChart");
    charts->addWidget(chartCard(QStringLiteral("站点实收排名"),stationRevenueChart_,page),0,1);
    paidOrdersChart_ = new RevenueChart(page);
    paidOrdersChart_->setMetric(QStringLiteral("已支付订单"),QStringLiteral("单"),1,QColor("#7463c7"));
    charts->addWidget(chartCard(QStringLiteral("每日已支付订单"),paidOrdersChart_,page),1,0);
    energyChart_ = new RevenueChart(page);
    energyChart_->setMetric(QStringLiteral("已支付订单电量"),QStringLiteral("kWh"),1000,QColor("#159b8d"));
    charts->addWidget(chartCard(QStringLiteral("每日已支付订单电量"),energyChart_,page),1,1);
    modeRevenueChart_ = new PileStatusChart(page);
    modeRevenueChart_->setCaption(QStringLiteral("实收  元"),true);
    modeRevenueChart_->setValueFormat(100,QStringLiteral("元"));
    charts->addWidget(chartCard(QStringLiteral("充电方式实收构成"),modeRevenueChart_,page),2,0);
    startPeriodChart_ = new AnalysisBarChart(page);
    charts->addWidget(chartCard(QStringLiteral("充电启动时段"),startPeriodChart_,page,
        QStringLiteral("区间已支付订单，按北京时间启动时段分组")),2,1);
    layout->addLayout(charts,1);
    return scrollAnalysis(page, "revenueAnalysisScroll");
}

QWidget *AdminWindow::buildOperationsPage()
{
    auto *page = new QWidget;
    page->setObjectName(QStringLiteral("managementPage"));
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0,0,0,0); layout->setSpacing(16);
    auto *clockRow = new QHBoxLayout;
    auto *clockCaption = heading(QStringLiteral("当前时间"), page, "muted");
    clockRow->addWidget(clockCaption);
    clockRow->addSpacing(4);
    operationsClock_ = new QLabel(page);
    operationsClock_->setObjectName(QStringLiteral("operationsClock"));
    operationsClock_->setStyleSheet(QStringLiteral("color:#334d70; font-size:20px; font-weight:500;"));
    operationsClock_->setAccessibleName(QStringLiteral("当前时间"));
    operationsClock_->setToolTip(QStringLiteral("当前时间；运营数据可通过右上角刷新更新"));
    const auto updateClock = [this] {
        operationsClock_->setText(adminTimeText(QDateTime::currentDateTimeUtc().toString(Qt::ISODate)));
    };
    updateClock();
    clockRow->addWidget(operationsClock_);
    clockRow->addStretch();
    layout->addLayout(clockRow);
    auto *clockTimer = new QTimer(page);
    clockTimer->setInterval(1000);
    connect(clockTimer, &QTimer::timeout, page, [this, updateClock] {
        if (operationsClock_->isVisible()) updateClock();
    });
    clockTimer->start();
    auto *metrics = new QGridLayout;
    metrics->setSpacing(14);
    metrics->addWidget(metricCard(QStringLiteral("运营中站点"),"#2f6fed",&operationsStations_,page),0,0);
    metrics->addWidget(metricCard(QStringLiteral("可用空闲桩"),"#159b8d",&operationsIdle_,page),0,1);
    metrics->addWidget(metricCard(QStringLiteral("充电 / 预约"),"#7463c7",&operationsInUse_,page),0,2);
    metrics->addWidget(metricCard(QStringLiteral("故障 / 离线"),"#d45252",&operationsAbnormal_,page),0,3);
    operationsIdle_->setObjectName("operationsAvailablePiles");
    layout->addLayout(metrics);
    operationsSummary_ = heading({},page,"muted");
    operationsSummary_->setWordWrap(true); layout->addWidget(operationsSummary_);
    operationsSummary_->hide();
    auto *charts = new QGridLayout;
    charts->setSpacing(16); charts->setColumnStretch(0,1); charts->setColumnStretch(1,1);
    pileStatusChart_ = new PileStatusChart(page);
    charts->addWidget(chartCard(QStringLiteral("电桩当前状态"),pileStatusChart_,page),0,0);
    stationOccupancyChart_ = new PileStatusChart(page);
    stationOccupancyChart_->setObjectName(QStringLiteral("stationOccupancyChart"));
    stationOccupancyChart_->setCaption(QStringLiteral("有桩站点"));
    stationOccupancyChart_->setValueFormat(1,QStringLiteral(" 站"));
    charts->addWidget(chartCard(QStringLiteral("站点占用率分布"),stationOccupancyChart_,page,
        QStringLiteral("按站点数量统计；占用率 = 充电与预约桩数 / 总桩数")),0,1);
    stationFaultChart_ = new AnalysisBarChart(page);
    charts->addWidget(chartCard(QStringLiteral("需关注站点"),stationFaultChart_,page,
        QStringLiteral("故障与离线电桩数量")),1,0);
    orderStatesChart_ = new PileStatusChart(page);
    orderStatesChart_->setCaption(QStringLiteral("订单总数"), true);
    charts->addWidget(chartCard(QStringLiteral("全部订单当前状态"),orderStatesChart_,page),1,1);
    layout->addLayout(charts);
    operationsTable_ = new QTableWidget(page);
    operationsTable_->setObjectName("operationsStationTable");
    prepareTable(operationsTable_, {QStringLiteral("站点"),QStringLiteral("总数"),QStringLiteral("空闲"),
        QStringLiteral("在用"),QStringLiteral("离线"),QStringLiteral("故障")});
    operationsTable_->horizontalHeader()->setSectionResizeMode(0,QHeaderView::Stretch);
    operationsTable_->verticalHeader()->setDefaultSectionSize(44);
    operationsTable_->setMinimumHeight(300);
    operationsTable_->setSelectionBehavior(QAbstractItemView::SelectItems);
    operationsTable_->setSelectionMode(QAbstractItemView::SingleSelection);
    operationsTable_->setCursor(Qt::PointingHandCursor);
    operationsTable_->setToolTip(QStringLiteral("点击站名查看站点，点击数量查看对应电桩；键盘选中后按回车打开"));
    layout->addWidget(chartCard(QStringLiteral("站点电桩明细"),operationsTable_,page));
    connect(pileStatusChart_, &PileStatusChart::statusClicked, this, &AdminWindow::navigateToPileStatus);
    return scrollAnalysis(page,"operationsAnalysisScroll");
}

QWidget *AdminWindow::buildStationsPage()
{
    auto *page = new QWidget(this);
    page->setObjectName(QStringLiteral("managementPage"));
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    auto *controls = new QHBoxLayout;
    controls->setSpacing(10);
    stationSearchField_ = new AdminComboBox(page);
    stationSearchField_->addItems({QStringLiteral("站点名称"), QStringLiteral("地址"), QStringLiteral("全部")});
    stationSearchField_->setCurrentIndex(2);
    stationSearch_ = new QLineEdit(page);
    stationSearch_->setPlaceholderText(QStringLiteral("搜索"));
    stationSearch_->setMaximumWidth(300);
    stationRegion_ = new AdminComboBox(page);
    stationRegion_->addItem(QStringLiteral("全部区域"), QString{});
    stationStatus_ = new AdminComboBox(page);
    stationStatus_->addItem(QStringLiteral("全部状态"), QString{});
    stationStatus_->addItem(QStringLiteral("启用"), QStringLiteral("ACTIVE"));
    stationStatus_->addItem(QStringLiteral("停用"), QStringLiteral("DISABLED"));
    auto *resetButton = new QPushButton(QStringLiteral("重置"), page);
    connect(stationSearch_, &QLineEdit::textChanged, this, [this](const QString &text) {
        appliedStationSearch_ = text.trimmed();
        refreshStations();
    });
    connect(stationSearchField_, &QComboBox::currentIndexChanged, this, [this] {
        if (stationSearch_ != nullptr && !stationSearch_->text().isEmpty()) refreshStations();
    });
    connect(resetButton, &QPushButton::clicked, this, [this] {
        const QSignalBlocker fieldBlocker(stationSearchField_);
        const QSignalBlocker textBlocker(stationSearch_);
        stationSearch_->clear(); appliedStationSearch_.clear();
        stationSearchField_->setCurrentIndex(2);
        if (stationClickTimer_ != nullptr) stationClickTimer_->stop();
        pendingStationClick_ = nullptr;
        if (stationsTable_ != nullptr) {
            for (int i = 0; i < stationsTable_->topLevelItemCount(); ++i)
                stationsTable_->topLevelItem(i)->setExpanded(false);
        }
        if (stationExpandToggle_ != nullptr) stationExpandToggle_->setText(QStringLiteral("全部展开"));
        stationOccupancy_ = -1;
        selectedStationRegions_.clear(); selectedStationStatuses_.clear(); updateFilterButton(stationRegionFilter_, 0); updateFilterButton(stationStatusFilter_, 0); refreshStations();
    });
    auto *createButton = new QPushButton(QStringLiteral("＋ 新增充电站"), page);
    createButton->setProperty("primary", true);
    connect(createButton, &QPushButton::clicked, this, &AdminWindow::showCreateStationDialog);
    stationExpandToggle_ = new QPushButton(QStringLiteral("全部展开"), page);
    connect(stationExpandToggle_, &QPushButton::clicked, this, [this] {
        bool anyExpanded = false;
        for (int i = 0; i < stationsTable_->topLevelItemCount(); ++i) {
            if (stationsTable_->topLevelItem(i)->isExpanded()) { anyExpanded = true; break; }
        }
        const bool expand = !anyExpanded;
        for (int i = 0; i < stationsTable_->topLevelItemCount(); ++i) stationsTable_->topLevelItem(i)->setExpanded(expand);
        stationExpandToggle_->setText(expand ? QStringLiteral("全部收起") : QStringLiteral("全部展开"));
    });
    stationRegionFilter_ = new QPushButton(page);
    stationRegionFilter_->setProperty("filterTitle", QStringLiteral("区域"));
    updateFilterButton(stationRegionFilter_, 0);
    stationStatusFilter_ = new QPushButton(page);
    stationStatusFilter_->setProperty("filterTitle", QStringLiteral("状态"));
    updateFilterButton(stationStatusFilter_, 0);
    installMultiSelectMenu(stationRegionFilter_, stationRegion_,
        [this](const QVariant &v){ return selectedStationRegions_.contains(v.toString()); },
        [this](const QVariant &v, bool on){ if(on) selectedStationRegions_.insert(v.toString()); else selectedStationRegions_.remove(v.toString()); updateFilterButton(stationRegionFilter_, selectedStationRegions_.size()); },
        [this]{ selectedStationRegions_.clear(); updateFilterButton(stationRegionFilter_, 0); }, [this]{ refreshStations(); });
    installMultiSelectMenu(stationStatusFilter_, stationStatus_,
        [this](const QVariant &v){ return selectedStationStatuses_.contains(v.toString()); },
        [this](const QVariant &v, bool on){ if(on) selectedStationStatuses_.insert(v.toString()); else selectedStationStatuses_.remove(v.toString()); updateFilterButton(stationStatusFilter_, selectedStationStatuses_.size()); },
        [this]{ selectedStationStatuses_.clear(); updateFilterButton(stationStatusFilter_, 0); }, [this]{ refreshStations(); });
    controls->addWidget(stationSearchField_);
    controls->addWidget(stationSearch_);
    controls->addWidget(stationRegionFilter_);
    controls->addWidget(stationStatusFilter_);
    stationOccupancyFilter_ = new QPushButton(page);
    stationOccupancyFilter_->setObjectName(QStringLiteral("stationOccupancyFilter"));
    stationOccupancyFilter_->setProperty("filterTitle",QStringLiteral("占用率"));
    updateFilterButton(stationOccupancyFilter_,0);
    connect(stationOccupancyFilter_,&QPushButton::clicked,this,[this] {
        QMenu menu(stationOccupancyFilter_);
        for (int band=-1;band<5;++band) {
            auto *action = menu.addAction(band<0 ? QStringLiteral("全部占用率") : stationOccupancyLabel(band));
            action->setCheckable(true); action->setChecked(stationOccupancy_==band);
            connect(action,&QAction::triggered,this,[this,band] { stationOccupancy_=band; refreshStations(); });
        }
        menu.exec(stationOccupancyFilter_->mapToGlobal(QPoint(0,stationOccupancyFilter_->height())));
    });
    controls->addWidget(stationOccupancyFilter_);
    controls->addWidget(resetButton);
    controls->addStretch();
    controls->addWidget(stationExpandToggle_);
    controls->addWidget(createButton);
    layout->addLayout(controls);
    stationsTable_ = new QTreeWidget(page);
    // Keep station rows aligned with the other management tables (48 px),
    // while retaining a slightly denser 40 px height for expanded pile rows.
    stationsTable_->setItemDelegate(new StationRowDelegate(stationsTable_));
    stationClickTimer_ = new QTimer(stationsTable_);
    stationClickTimer_->setSingleShot(true);
    connect(stationClickTimer_, &QTimer::timeout, this, [this] {
        if (pendingStationClick_ != nullptr) {
            pendingStationClick_->setExpanded(!pendingStationClick_->isExpanded());
            pendingStationClick_ = nullptr;
        }
    });
    stationsTable_->setColumnCount(7);
    stationsTable_->setHeaderLabels({QStringLiteral("ID"), QStringLiteral("站点"), QStringLiteral("区域"),
                                     QStringLiteral("可用 / 总数"), QStringLiteral("在线率"), QStringLiteral("基础电价"), QStringLiteral("状态")});
    stationsTable_->setRootIsDecorated(true);
    // 展开/收起只允许通过左侧树形小三角，双击行仅用于打开详情。
    stationsTable_->setExpandsOnDoubleClick(false);
    stationsTable_->setAlternatingRowColors(true);
    stationsTable_->setMouseTracking(true);
    stationsTable_->header()->setFixedHeight(44);
    stationsTable_->setSelectionMode(QAbstractItemView::SingleSelection);
    stationsTable_->header()->setSectionResizeMode(QHeaderView::ResizeToContents);
    stationsTable_->header()->setSectionResizeMode(1, QHeaderView::Stretch);
    stationsTable_->header()->setSectionsMovable(false);
    stationsTable_->header()->setSectionsClickable(true);
    stationsTable_->setContextMenuPolicy(Qt::CustomContextMenu);
    stationRegion_->hide(); stationStatus_->hide();
    stationsTable_->setIndentation(24);
    connect(stationsTable_, &QTreeWidget::itemDoubleClicked, this,
            [this](QTreeWidgetItem *clicked, int) {
                if (!clicked) return;
                if (stationClickTimer_ != nullptr) stationClickTimer_->stop();
                pendingStationClick_ = nullptr;
                if (clicked->parent() == nullptr) showStationDetails(clicked->data(0, Qt::UserRole).toLongLong());
                else navigateToPile(clicked->data(0, Qt::UserRole + 1).toLongLong(), clicked->data(0, Qt::UserRole).toLongLong());
            });
    connect(stationsTable_, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem *clicked, int) {
        if (clicked == nullptr || clicked->parent() != nullptr) return;
        pendingStationClick_ = clicked;
        // 使用较短的单击判定窗口，避免等待系统双击间隔造成明显停顿；
        // 双击事件到达时仍会取消此待执行动作。
        if (stationClickTimer_ != nullptr) stationClickTimer_->start(160);
    });
    connect(stationsTable_, &QTreeWidget::itemExpanded, this, [this](QTreeWidgetItem *) {
        if (stationExpandToggle_ != nullptr) stationExpandToggle_->setText(QStringLiteral("全部收起"));
    });
    connect(stationsTable_, &QTreeWidget::itemCollapsed, this, [this](QTreeWidgetItem *) {
        bool anyExpanded = false;
        for (int i = 0; i < stationsTable_->topLevelItemCount(); ++i) if (stationsTable_->topLevelItem(i)->isExpanded()) { anyExpanded = true; break; }
        if (stationExpandToggle_ != nullptr) stationExpandToggle_->setText(anyExpanded ? QStringLiteral("全部收起") : QStringLiteral("全部展开"));
    });
    connect(stationsTable_, &QTreeWidget::customContextMenuRequested, this, [this](const QPoint &pos) {
        auto *it = stationsTable_->itemAt(pos); if (!it) return;
        stationsTable_->setCurrentItem(it); const qint64 id = it->parent() ? it->data(0, Qt::UserRole).toLongLong() : it->data(0, Qt::UserRole).toLongLong();
        QMenu menu(this);
        if (it->parent()) {
            menu.addAction(QStringLiteral("查看电桩详情"), this, [this, it] { showPileDetails(it->data(0, Qt::UserRole+1).toLongLong()); });
            menu.addAction(QStringLiteral("前往充电桩管理"), this, [this, it] { navigateToPile(it->data(0, Qt::UserRole+1).toLongLong(), it->data(0, Qt::UserRole).toLongLong()); });
        } else {
            menu.addAction(QStringLiteral("查看详情"), this, [this, it] { showStationDetails(it->data(0, Qt::UserRole).toLongLong()); });
            menu.addAction(QStringLiteral("管理站内充电桩"), this, [this, it] {
                navigateToStationPiles(it->data(0, Qt::UserRole).toLongLong());
            });
            menu.addAction(QStringLiteral("编辑充电站信息"), this, [this, it] {
                showEditStationDialog(it->data(0, Qt::UserRole).toLongLong());
            });
            const qint64 stationId = it->data(0, Qt::UserRole).toLongLong();
            const bool active = it->text(6) == QStringLiteral("启用");
            menu.addAction(active ? QStringLiteral("停用充电站") : QStringLiteral("启用充电站"),
                           this, [this, stationId, active] {
                               toggleStationStatus(stationId, active);
                           });
            auto *addPile = menu.addAction(QStringLiteral("新增充电桩"));
            addPile->setEnabled(it->text(6) == QStringLiteral("启用"));
            connect(addPile, &QAction::triggered, this, [this,id]{ showCreatePileDialog(id); });
            menu.addAction(QStringLiteral("删除站点"), this, &AdminWindow::deleteSelectedStation);
        }
        menu.exec(stationsTable_->viewport()->mapToGlobal(pos));
    });
    layout->addWidget(stationsTable_, 1);
    return page;
}

QWidget *AdminWindow::buildPilesPage()
{
    auto *page = new QWidget(this);
    page->setObjectName(QStringLiteral("managementPage"));
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    auto *controls = new QHBoxLayout;
    controls->setSpacing(10);
    pileSearchField_ = new AdminComboBox(page);
    pileSearchField_->addItems({QStringLiteral("电桩编号"), QStringLiteral("所属站点"), QStringLiteral("全部")});
    pileSearchField_->setCurrentIndex(2);
    controls->addWidget(pileSearchField_);
    pileSearch_ = new QLineEdit(page);
    pileSearch_->setPlaceholderText(QStringLiteral("搜索"));
    pileSearch_->setMaximumWidth(240);
    pileStation_ = new AdminComboBox(page);
    pileStation_->addItem(QStringLiteral("全部站点"), QVariant{});
    pileStatus_ = new AdminComboBox(page);
    pileStatus_->addItem(QStringLiteral("全部状态"), QString{});
    for (const QString &status : {QStringLiteral("IDLE"), QStringLiteral("RESERVED"), QStringLiteral("CHARGING"), QStringLiteral("FAULT"), QStringLiteral("OFFLINE")}) {
        pileStatus_->addItem(pileStatusText(status), status);
    }
    auto *resetButton = new QPushButton(QStringLiteral("重置"), page);
    connect(pileSearch_, &QLineEdit::textChanged, this, [this](const QString &text) {
        appliedPileSearch_ = text.trimmed();
        refreshPiles();
    });
    connect(pileSearchField_, &QComboBox::currentIndexChanged, this, [this] {
        if (pileSearch_ != nullptr && !pileSearch_->text().isEmpty()) refreshPiles();
    });
    connect(resetButton, &QPushButton::clicked, this, [this] {
        const QSignalBlocker fieldBlocker(pileSearchField_);
        const QSignalBlocker textBlocker(pileSearch_);
        pileSearch_->clear(); appliedPileSearch_.clear();
        pileSearchField_->setCurrentIndex(2);
        pilesActiveStationsOnly_ = false;
        selectedPileStations_.clear(); selectedPileStatuses_.clear(); updateFilterButton(pileStationFilter_, 0); updateFilterButton(pileStatusFilter_, 0); refreshPiles();
    });
    controls->addWidget(pileSearch_);
    pileActiveScope_ = new QPushButton(QStringLiteral("运营中站点 ×"), page);
    pileActiveScope_->setObjectName(QStringLiteral("pileActiveStationScope"));
    pileActiveScope_->hide();
    connect(pileActiveScope_, &QPushButton::clicked, this, [this] { pilesActiveStationsOnly_=false; refreshPiles(); });
    controls->addWidget(pileActiveScope_);
    auto *createButton = new QPushButton(QStringLiteral("＋ 新增电桩"), page);
    createButton->setProperty("primary", true);
    connect(createButton, &QPushButton::clicked, this,
            [this] { showCreatePileDialog(); });
    pilesTable_ = new QTableWidget(page);
    pilesTable_->setObjectName(QStringLiteral("pilesTable"));
    prepareTable(pilesTable_, {QStringLiteral("ID"), QStringLiteral("电桩编号"), QStringLiteral("所属站点"), QStringLiteral("类型"), QStringLiteral("状态")});
    pilesTable_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    pilesTable_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    pilesTable_->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    pilesTable_->verticalHeader()->setDefaultSectionSize(48);
    connect(pilesTable_, &QTableWidget::cellDoubleClicked, this, [this](int row, int){ if (row >= 0) showPileDetails(pilesTable_->item(row,0)->data(Qt::UserRole).toLongLong()); });
    pilesTable_->setContextMenuPolicy(Qt::CustomContextMenu);
    pileStation_->hide(); pileStatus_->hide();
    connect(pilesTable_, &QTableWidget::customContextMenuRequested, this, [this](const QPoint &pos){
        const int row = pilesTable_->rowAt(pos.y()); if (row < 0) return; pilesTable_->selectRow(row);
        const QString status = pilesTable_->item(row, 4)->data(Qt::UserRole).toString();
        QMenu menu(this);
        menu.addAction(QStringLiteral("查看详情"), this, [this,row]{ showPileDetails(pilesTable_->item(row,0)->data(Qt::UserRole).toLongLong()); });
        menu.setObjectName(QStringLiteral("pileContextMenu"));
        const qint64 pileId = pilesTable_->item(row,0)->data(Qt::UserRole).toLongLong();
        menu.addAction(QStringLiteral("定位到充电站"), this,
                       [this, pileId] { navigateToPileStation(pileId); });
        auto *edit = menu.addAction(QStringLiteral("修改电桩信息"));
        edit->setEnabled(status != QStringLiteral("RESERVED")
                         && status != QStringLiteral("CHARGING"));
        connect(edit, &QAction::triggered, this, [this,row]{
            showEditPileDialog(pilesTable_->item(row, 0)->data(Qt::UserRole).toLongLong());
        });
        auto *on = menu.addAction(QStringLiteral("开机/上线")); on->setEnabled(status == QStringLiteral("OFFLINE"));
        connect(on, &QAction::triggered, this, [this,row]{ auto x=facade_->setPileStatus(pilesTable_->item(row,0)->data(Qt::UserRole).toLongLong(),PileStatus::Idle); if(!x.ok())showServiceError(x.code,x.message); else refreshAll(); });
        auto *off = menu.addAction(QStringLiteral("关机/下线")); off->setEnabled(status == QStringLiteral("IDLE"));
        connect(off, &QAction::triggered, this, [this,row]{ auto x=facade_->setPileStatus(pilesTable_->item(row,0)->data(Qt::UserRole).toLongLong(),PileStatus::Offline); if(!x.ok())showServiceError(x.code,x.message); else refreshAll(); });
        auto *restart = menu.addAction(QStringLiteral("重启")); restart->setEnabled(status == QStringLiteral("IDLE") || status == QStringLiteral("OFFLINE")); connect(restart, &QAction::triggered, this, &AdminWindow::restartSelectedPile);
        auto *fault = menu.addAction(QStringLiteral("标记故障")); fault->setEnabled(status == QStringLiteral("IDLE") || status == QStringLiteral("OFFLINE"));
        connect(fault, &QAction::triggered, this, [this,row]{ auto x=facade_->setPileStatus(pilesTable_->item(row,0)->data(Qt::UserRole).toLongLong(),PileStatus::Fault); if(!x.ok())showServiceError(x.code,x.message); else refreshAll(); });
        auto *del = menu.addAction(QStringLiteral("删除")); del->setEnabled(status == QStringLiteral("IDLE") || status == QStringLiteral("OFFLINE")); connect(del, &QAction::triggered, this, &AdminWindow::deleteSelectedPile); menu.exec(pilesTable_->viewport()->mapToGlobal(pos));
    });
    pileStationFilter_ = new QPushButton(page); pileStationFilter_->setProperty("filterTitle", QStringLiteral("站点")); updateFilterButton(pileStationFilter_, 0);
    pileStatusFilter_ = new QPushButton(page); pileStatusFilter_->setProperty("filterTitle", QStringLiteral("状态")); updateFilterButton(pileStatusFilter_, 0);
    installMultiSelectMenu(pileStationFilter_, pileStation_,
        [this](const QVariant &v){ return selectedPileStations_.contains(v.toLongLong()); },
        [this](const QVariant &v, bool on){ const qint64 id = v.toLongLong(); if (on) selectedPileStations_.insert(id); else selectedPileStations_.remove(id); updateFilterButton(pileStationFilter_, selectedPileStations_.size()); },
        [this]{ selectedPileStations_.clear(); updateFilterButton(pileStationFilter_, 0); },
        [this]{ refreshPiles(); });
    installMultiSelectMenu(pileStatusFilter_, pileStatus_, [this](const QVariant &v){ return selectedPileStatuses_.contains(v.toString()); }, [this](const QVariant &v,bool on){ if(on) selectedPileStatuses_.insert(v.toString()); else selectedPileStatuses_.remove(v.toString()); updateFilterButton(pileStatusFilter_, selectedPileStatuses_.size()); }, [this]{ selectedPileStatuses_.clear(); updateFilterButton(pileStatusFilter_, 0); }, [this]{ refreshPiles(); });
    controls->addWidget(pileStationFilter_);
    controls->addWidget(pileStatusFilter_);
    controls->addWidget(resetButton);
    controls->addStretch();
    controls->addWidget(createButton);
    layout->addLayout(controls);
    layout->addWidget(pilesTable_, 1);
    return page;
}

QWidget *AdminWindow::buildUsersPage()
{
    auto *page = new QWidget(this);
    page->setObjectName(QStringLiteral("managementPage"));
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    auto *controls = new QHBoxLayout;
    controls->setSpacing(10);
    userSearchField_ = new AdminComboBox(page);
    userSearchField_->addItems({QStringLiteral("手机号"), QStringLiteral("昵称"), QStringLiteral("全部")});
    userSearchField_->setCurrentIndex(2);
    controls->addWidget(userSearchField_);
    userSearch_ = new QLineEdit(page);
    userSearch_->setPlaceholderText(QStringLiteral("搜索"));
    userSearch_->setMaximumWidth(300);
    auto *resetButton = new QPushButton(QStringLiteral("重置"), page);
    connect(userSearch_, &QLineEdit::textChanged, this, [this](const QString &text) {
        appliedUserSearch_ = text.trimmed();
        refreshUsers();
    });
    connect(userSearchField_, &QComboBox::currentIndexChanged, this, [this] {
        if (userSearch_ != nullptr && !userSearch_->text().isEmpty()) refreshUsers();
    });
    controls->addWidget(userSearch_);
    userStatus_ = new AdminComboBox(page);
    userStatus_->addItem(QStringLiteral("全部状态"), QString{});
    userStatus_->addItem(QStringLiteral("正常"), QStringLiteral("ACTIVE"));
    userStatus_->addItem(QStringLiteral("已冻结"), QStringLiteral("FROZEN"));
    userStatusFilter_ = new QPushButton(page); userStatusFilter_->setProperty("filterTitle", QStringLiteral("状态")); updateFilterButton(userStatusFilter_, 0);
    installMultiSelectMenu(userStatusFilter_, userStatus_, [this](const QVariant &v){ return selectedUserStatuses_.contains(v.toString()); }, [this](const QVariant &v,bool on){ if(on) selectedUserStatuses_.insert(v.toString()); else selectedUserStatuses_.remove(v.toString()); updateFilterButton(userStatusFilter_, selectedUserStatuses_.size()); }, [this]{ selectedUserStatuses_.clear(); updateFilterButton(userStatusFilter_, 0); }, [this]{ refreshUsers(); });
    connect(resetButton, &QPushButton::clicked, this, [this] {
        const QSignalBlocker fieldBlocker(userSearchField_);
        const QSignalBlocker textBlocker(userSearch_);
        userSearch_->clear(); appliedUserSearch_.clear(); userSearchField_->setCurrentIndex(2);
        selectedUserStatuses_.clear(); updateFilterButton(userStatusFilter_, 0); refreshUsers();
    });
    controls->addWidget(userStatusFilter_);
    controls->addWidget(resetButton);
    controls->addStretch();
    layout->addLayout(controls);
    usersTable_ = new QTableWidget(page);
    prepareTable(usersTable_, {QStringLiteral("ID"), QStringLiteral("手机号"), QStringLiteral("昵称"), QStringLiteral("余额"), QStringLiteral("状态")});
    usersTable_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    usersTable_->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    usersTable_->verticalHeader()->setDefaultSectionSize(48);
    connect(usersTable_, &QTableWidget::cellDoubleClicked, this, [this](int row,int){ if(row>=0) showUserDetails(usersTable_->item(row,0)->data(Qt::UserRole).toLongLong()); });
    usersTable_->setContextMenuPolicy(Qt::CustomContextMenu);
    userStatus_->hide();
    connect(usersTable_, &QTableWidget::customContextMenuRequested, this, [this](const QPoint &pos){ const int row=usersTable_->rowAt(pos.y()); if(row<0)return; usersTable_->selectRow(row); QMenu menu(this); menu.addAction(QStringLiteral("查看详情"), this,[this,row]{showUserDetails(usersTable_->item(row,0)->data(Qt::UserRole).toLongLong());}); menu.addAction(QStringLiteral("冻结/解冻"),this,&AdminWindow::toggleSelectedUserStatus); menu.exec(usersTable_->viewport()->mapToGlobal(pos)); });
    layout->addWidget(usersTable_, 1);
    return page;
}

QWidget *AdminWindow::buildOrdersPage()
{
    auto *page = new QWidget(this);
    page->setObjectName(QStringLiteral("managementPage"));
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    auto *controls = new QHBoxLayout;
    controls->setSpacing(10);
    orderSearchField_ = new AdminComboBox(page);
    orderSearchField_->addItems({QStringLiteral("订单号"), QStringLiteral("用户手机号"), QStringLiteral("电桩编号"), QStringLiteral("全部")});
    orderSearchField_->setCurrentIndex(3);
    orderSearchField_->setFixedWidth(110);
    controls->addWidget(orderSearchField_);
    orderSearch_ = new QLineEdit(page);
    orderSearch_->setObjectName(QStringLiteral("orderSearch"));
    orderSearch_->setPlaceholderText(QStringLiteral("搜索"));
    orderSearch_->setMinimumWidth(80);
    orderSearch_->setMaximumWidth(260);
    auto *resetButton = new QPushButton(QStringLiteral("重置"), page);
    orderStatus_ = new AdminComboBox(page);
    orderStatus_->addItem(QStringLiteral("全部状态"), QString{});
    for (const QString &status : {QStringLiteral("RESERVED"), QStringLiteral("CHARGING"), QStringLiteral("PENDING_PAYMENT"), QStringLiteral("COMPLETED"), QStringLiteral("CANCELLED")}) {
        orderStatus_->addItem(orderStatusText(status), status);
    }
    orderMode_ = new AdminComboBox(page);
    orderMode_->addItem(QStringLiteral("全部模式"), QString{});
    orderMode_->addItem(QStringLiteral("直接充电"), QStringLiteral("DIRECT"));
    orderMode_->addItem(QStringLiteral("预约"), QStringLiteral("RESERVATION"));
    connect(orderSearch_, &QLineEdit::textChanged, this, [this](const QString &text) {
        appliedOrderSearch_ = text.trimmed();
        refreshOrders();
    });
    connect(orderSearchField_, &QComboBox::currentIndexChanged, this, [this] {
        if (orderSearch_ != nullptr && !orderSearch_->text().isEmpty()) refreshOrders();
    });
    connect(orderStatus_, &QComboBox::currentIndexChanged, this, &AdminWindow::refreshOrders);
    connect(orderMode_, &QComboBox::currentIndexChanged, this, &AdminWindow::refreshOrders);
    connect(resetButton, &QPushButton::clicked, this, [this] {
        const QSignalBlocker fieldBlocker(orderSearchField_);
        const QSignalBlocker textBlocker(orderSearch_);
        orderSearch_->clear(); appliedOrderSearch_.clear();
        orderSearchField_->setCurrentIndex(3);
        selectedOrderStations_.clear(); orderStartPeriod_ = -1;
        resetOrderTimeFilter();
        selectedOrderStatuses_.clear(); selectedOrderModes_.clear(); updateFilterButton(orderStatusFilter_, 0); updateFilterButton(orderModeFilter_, 0); refreshOrders();
    });
    controls->addWidget(orderSearch_);
    orderStatusFilter_ = new QPushButton(page); orderStatusFilter_->setProperty("filterTitle", QStringLiteral("状态")); updateFilterButton(orderStatusFilter_, 0);
    orderModeFilter_ = new QPushButton(page); orderModeFilter_->setProperty("filterTitle", QStringLiteral("模式")); updateFilterButton(orderModeFilter_, 0);
    installMultiSelectMenu(orderStatusFilter_, orderStatus_, [this](const QVariant &v){ return selectedOrderStatuses_.contains(v.toString()); }, [this](const QVariant &v,bool on){ if(on) selectedOrderStatuses_.insert(v.toString()); else selectedOrderStatuses_.remove(v.toString()); updateFilterButton(orderStatusFilter_, selectedOrderStatuses_.size()); }, [this]{ selectedOrderStatuses_.clear(); updateFilterButton(orderStatusFilter_, 0); }, [this]{ refreshOrders(); });
    installMultiSelectMenu(orderModeFilter_, orderMode_, [this](const QVariant &v){ return selectedOrderModes_.contains(v.toString()); }, [this](const QVariant &v,bool on){ if(on) selectedOrderModes_.insert(v.toString()); else selectedOrderModes_.remove(v.toString()); updateFilterButton(orderModeFilter_, selectedOrderModes_.size()); }, [this]{ selectedOrderModes_.clear(); updateFilterButton(orderModeFilter_, 0); }, [this]{ refreshOrders(); });
    controls->addWidget(orderStatusFilter_);
    orderStation_ = new AdminComboBox(page);
    orderStation_->hide();
    orderStation_->addItem(QStringLiteral("全部站点"), QVariant());
    orderStationFilter_ = new QPushButton(page);
    orderStationFilter_->setObjectName(QStringLiteral("orderStationFilter"));
    orderStationFilter_->setProperty("filterTitle", QStringLiteral("站点"));
    updateFilterButton(orderStationFilter_,0);
    installMultiSelectMenu(orderStationFilter_, orderStation_,
        [this](const QVariant &v) { return selectedOrderStations_.contains(v.toLongLong()); },
        [this](const QVariant &v,bool selected) { if (selected) selectedOrderStations_.insert(v.toLongLong()); else selectedOrderStations_.remove(v.toLongLong()); },
        [this] { selectedOrderStations_.clear(); }, [this] { refreshOrders(); });
    controls->addWidget(orderStationFilter_);
    orderPeriodFilter_ = new QPushButton(page);
    orderPeriodFilter_->setObjectName(QStringLiteral("orderPeriodFilter"));
    orderPeriodFilter_->setProperty("filterTitle", QStringLiteral("时段"));
    orderPeriodFilter_->setToolTip(QStringLiteral("按充电开始时段筛选（北京时间），支持单选"));
    updateFilterButton(orderPeriodFilter_,0);
    connect(orderPeriodFilter_, &QPushButton::clicked, this, [this] {
        QMenu menu(orderPeriodFilter_);
        menu.setObjectName(QStringLiteral("orderPeriodMenu"));
        for (int period=-1;period<6;++period) {
            const QString label = period<0 ? QStringLiteral("全部时段") : QStringLiteral("%1:00–%2:00")
                .arg(period*4,2,10,QChar('0')).arg((period+1)*4,2,10,QChar('0'));
            auto *action = menu.addAction(label);
            action->setCheckable(true); action->setChecked(orderStartPeriod_==period);
            connect(action,&QAction::triggered,this,[this,period] { orderStartPeriod_=period; refreshOrders(); });
        }
        menu.exec(orderPeriodFilter_->mapToGlobal(QPoint(0,orderPeriodFilter_->height())));
    });
    controls->addWidget(orderPeriodFilter_);
    controls->addWidget(orderModeFilter_);
    orderTimeFilter_ = new QPushButton(page);
    orderTimeFilter_->setObjectName(QStringLiteral("orderTimeFilter"));
    orderTimeFilter_->setProperty("filterTitle", QStringLiteral("时间"));
    updateOrderTimeFilterButton();
    connect(orderTimeFilter_, &QPushButton::clicked, this, &AdminWindow::showOrderTimeFilter);
    controls->addWidget(orderTimeFilter_);
    controls->addWidget(resetButton);
    controls->addStretch();
    layout->addLayout(controls);
    ordersTable_ = new QTableWidget(page);
    ordersTable_->setObjectName(QStringLiteral("ordersTable"));
    prepareTable(ordersTable_, {QStringLiteral("订单 ID"), QStringLiteral("订单号"), QStringLiteral("用户手机号"), QStringLiteral("站点"), QStringLiteral("电桩"), QStringLiteral("模式"), QStringLiteral("状态"), QStringLiteral("金额"), QStringLiteral("创建时间"), QStringLiteral("支付时间")});
    ordersTable_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    ordersTable_->horizontalHeader()->setSectionResizeMode(7, QHeaderView::ResizeToContents);
    ordersTable_->horizontalHeader()->setSectionResizeMode(8, QHeaderView::ResizeToContents);
    ordersTable_->horizontalHeader()->setSectionResizeMode(9, QHeaderView::ResizeToContents);
    ordersTable_->horizontalHeader()->setSortIndicator(8, Qt::DescendingOrder);
    ordersTable_->horizontalHeader()->setSortIndicatorShown(true);
    ordersTable_->horizontalHeaderItem(8)->setToolTip(QStringLiteral("默认按创建时间从新到旧排列（北京时间）"));
    ordersTable_->verticalHeader()->setDefaultSectionSize(48);
    connect(ordersTable_, &QTableWidget::cellDoubleClicked, this, [this](int row,int){ if(row>=0) showOrderDetails(ordersTable_->item(row,0)->data(Qt::UserRole).toLongLong()); });
    ordersTable_->setContextMenuPolicy(Qt::CustomContextMenu);
    orderStatus_->hide(); orderMode_->hide();
    connect(ordersTable_, &QTableWidget::customContextMenuRequested, this, [this](const QPoint &pos) {
        const int row = ordersTable_->rowAt(pos.y());
        if (row < 0) return;
        ordersTable_->setCurrentCell(row, 0);
        ordersTable_->selectRow(row);
        const qint64 orderId = ordersTable_->item(row, 0)->data(Qt::UserRole).toLongLong();
        const QString orderNo = ordersTable_->item(row, 1)->text();
        const qint64 stationId = ordersTable_->item(row, 3)->data(Qt::UserRole).toLongLong();
        const qint64 pileId = ordersTable_->item(row, 4)->data(Qt::UserRole).toLongLong();
        QMenu menu(this);
        menu.setObjectName(QStringLiteral("orderContextMenu"));
        menu.addAction(QStringLiteral("查看详情"), this, [this, orderId] { showOrderDetails(orderId); });
        menu.addAction(QStringLiteral("复制订单号"), this, [orderNo] { QApplication::clipboard()->setText(orderNo); });
        if (currentAdminRole_ == QStringLiteral("SYS_ADMIN")
            || currentAdminRole_ == QStringLiteral("STATION_ADMIN")) {
            menu.addSeparator();
            auto *stationAction = menu.addAction(QStringLiteral("管理该充电站"), this, [this, stationId, pileId] {
                PageState destination;
                destination.pageIndex = 2;
                destination.selectedStationId = stationId;
                destination.selectedStationPileId = pileId;
                destination.expandedStations.insert(stationId);
                navigateToAnalysisPage(destination);
            });
            stationAction->setEnabled(stationId > 0);
            auto *pileAction = menu.addAction(QStringLiteral("管理该充电桩"), this,
                [this, pileId, stationId] { navigateToPile(pileId, stationId); });
            pileAction->setEnabled(pileId > 0 && stationId > 0);
        }
        menu.exec(ordersTable_->viewport()->mapToGlobal(pos));
    });
    layout->addWidget(ordersTable_, 1);
    return page;
}

QWidget *AdminWindow::buildAdminsPage()
{
    auto *page = new QWidget(this);
    page->setObjectName(QStringLiteral("managementPage"));
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    auto *controls = new QHBoxLayout;
    controls->setSpacing(10);
    adminSearch_ = new QLineEdit(page);
    adminSearch_->setObjectName(QStringLiteral("adminSearch"));
    adminSearch_->setPlaceholderText(QStringLiteral("搜索账号或显示名"));
    adminSearch_->setMaximumWidth(300);
    connect(adminSearch_, &QLineEdit::textChanged, this, [this](const QString &text) {
        appliedAdminSearch_ = text.trimmed();
        refreshAdmins();
    });
    controls->addWidget(adminSearch_);
    adminRole_ = new AdminComboBox(page);
    adminRole_->addItem(QStringLiteral("全部角色"), QString{});
    adminRole_->addItem(QStringLiteral("系统管理员"), QStringLiteral("SYS_ADMIN"));
    adminRole_->addItem(QStringLiteral("站点管理员"), QStringLiteral("STATION_ADMIN"));
    adminRole_->addItem(QStringLiteral("用户管理员"), QStringLiteral("USER_ADMIN"));
    adminRoleFilter_ = new QPushButton(page);
    adminRoleFilter_->setProperty("filterTitle", QStringLiteral("角色"));
    updateFilterButton(adminRoleFilter_, 0);
    installMultiSelectMenu(
        adminRoleFilter_, adminRole_,
        [this](const QVariant &value) {
            return selectedAdminRoles_.contains(value.toString());
        },
        [this](const QVariant &value, bool selected) {
            if (selected) selectedAdminRoles_.insert(value.toString());
            else selectedAdminRoles_.remove(value.toString());
            updateFilterButton(adminRoleFilter_, selectedAdminRoles_.size());
        },
        [this] {
            selectedAdminRoles_.clear();
            updateFilterButton(adminRoleFilter_, 0);
        },
        [this] { refreshAdmins(); });
    adminRole_->hide();
    controls->addWidget(adminRoleFilter_);
    adminStatus_ = new AdminComboBox(page);
    adminStatus_->addItem(QStringLiteral("全部状态"), QString{});
    adminStatus_->addItem(QStringLiteral("启用"), QStringLiteral("ACTIVE"));
    adminStatus_->addItem(QStringLiteral("停用"), QStringLiteral("DISABLED"));
    adminStatusFilter_ = new QPushButton(page);
    adminStatusFilter_->setProperty("filterTitle", QStringLiteral("状态"));
    updateFilterButton(adminStatusFilter_, 0);
    installMultiSelectMenu(
        adminStatusFilter_, adminStatus_,
        [this](const QVariant &value) {
            return selectedAdminStatuses_.contains(value.toString());
        },
        [this](const QVariant &value, bool selected) {
            if (selected) selectedAdminStatuses_.insert(value.toString());
            else selectedAdminStatuses_.remove(value.toString());
            updateFilterButton(adminStatusFilter_, selectedAdminStatuses_.size());
        },
        [this] {
            selectedAdminStatuses_.clear();
            updateFilterButton(adminStatusFilter_, 0);
        },
        [this] { refreshAdmins(); });
    adminStatus_->hide();
    controls->addWidget(adminStatusFilter_);
    auto *resetButton = new QPushButton(QStringLiteral("重置"), page);
    connect(resetButton, &QPushButton::clicked, this, [this] {
        const QSignalBlocker blocker(adminSearch_);
        adminSearch_->clear();
        appliedAdminSearch_.clear();
        selectedAdminStatuses_.clear();
        selectedAdminRoles_.clear();
        updateFilterButton(adminStatusFilter_, 0);
        updateFilterButton(adminRoleFilter_, 0);
        refreshAdmins();
    });
    controls->addWidget(resetButton);
    controls->addStretch();
    auto *createButton = new QPushButton(QStringLiteral("＋ 新增管理员"), page);
    createButton->setProperty("primary", true);
    createButton->setObjectName(QStringLiteral("createAdminButton"));
    connect(createButton, &QPushButton::clicked,
            this, &AdminWindow::showCreateAdminDialog);
    controls->addWidget(createButton);
    layout->addLayout(controls);

    adminsTable_ = new QTableWidget(page);
    adminsTable_->setObjectName(QStringLiteral("adminsTable"));
    prepareTable(adminsTable_, {QStringLiteral("ID"), QStringLiteral("账号"),
                                QStringLiteral("显示名"), QStringLiteral("角色"),
                                QStringLiteral("站点范围"), QStringLiteral("状态"),
                                QStringLiteral("最后登录")});
    adminsTable_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    adminsTable_->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Stretch);
    adminsTable_->horizontalHeader()->setSectionResizeMode(6, QHeaderView::Stretch);
    adminsTable_->verticalHeader()->setDefaultSectionSize(48);
    connect(adminsTable_, &QTableWidget::cellDoubleClicked, this,
            [this](int row, int) {
                if (row >= 0) {
                    showAdminDetails(
                        adminsTable_->item(row, 0)->data(Qt::UserRole).toLongLong());
                }
            });
    adminsTable_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(adminsTable_, &QTableWidget::customContextMenuRequested, this,
            [this](const QPoint &pos) {
                const int row = adminsTable_->rowAt(pos.y());
                if (row < 0) return;
                adminsTable_->selectRow(row);
                const qint64 adminId =
                    adminsTable_->item(row, 0)->data(Qt::UserRole).toLongLong();
                QMenu menu(this);
                menu.setObjectName(QStringLiteral("adminContextMenu"));
                menu.addAction(QStringLiteral("查看详情"), this,
                               [this, adminId] { showAdminDetails(adminId); });
                menu.addAction(QStringLiteral("修改管理员信息"), this,
                               [this, adminId] { showEditAdminDialog(adminId); });
                menu.exec(adminsTable_->viewport()->mapToGlobal(pos));
            });
    layout->addWidget(adminsTable_, 1);
    return page;
}

void AdminWindow::setLoginError(const QString &message)
{
    if (loginError_ == nullptr) return;
    loginError_->setText(message);
    loginError_->setProperty("hasError", !message.isEmpty());
    // Dynamic QSS properties are evaluated when the widget is polished.  A
    // repolish updates the reserved slot without changing its fixed height.
    if (loginError_->style() != nullptr) {
        loginError_->style()->unpolish(loginError_);
        loginError_->style()->polish(loginError_);
    }
    loginError_->update();
}

void AdminWindow::attemptLogin()
{
    passwordEdit_->findChild<QToolButton *>("loginPasswordToggle")->setChecked(false);
    if (usernameEdit_->text().trimmed().isEmpty() || passwordEdit_->text().isEmpty()) {
        setLoginError(QStringLiteral("请输入账号和密码"));
        (usernameEdit_->text().trimmed().isEmpty() ? usernameEdit_ : passwordEdit_)->setFocus();
        return;
    }
    if (facade_ == nullptr) {
        setLoginError(QStringLiteral("管理服务尚未初始化"));
        return;
    }
    currentAdminId_ = 0;
    currentAdminRole_.clear();
    currentAdminAccountsAvailable_ = false;
    supportTicketsPage_->clear();
    backHistory_.clear();
    forwardHistory_.clear();
    rootStack_->setCurrentIndex(0);
    const ServiceResult result = facade_->login(usernameEdit_->text().trimmed(), passwordEdit_->text());
    if (!result.ok()) {
        setLoginError(QStringLiteral("账号或密码错误，请重试"));
        passwordEdit_->selectAll();
        passwordEdit_->setFocus();
        return;
    }
    const QJsonObject admin = result.data.value(QStringLiteral("admin")).toObject();
    currentAdminId_ = admin.value(QStringLiteral("adminId")).toInteger();
    currentAdminRole_ = admin.value(QStringLiteral("role")).toString();
    applyAdminPermissions(admin);
    setLoginError(QString());
    if (admin.value(QStringLiteral("mustChangePassword")).toBool()) {
        if (!showChangePasswordDialog(true)) {
            facade_->logout();
            currentAdminId_ = 0;
            currentAdminRole_.clear();
            currentAdminAccountsAvailable_ = false;
        }
        return;
    }
    rootStack_->setCurrentIndex(1);
    visitedAnalysisPages_.clear();
    backHistory_.clear();
    forwardHistory_.clear();
    restoringHistory_ = true;
    navigation_->setCurrentRow(currentAdminRole_ == QStringLiteral("USER_ADMIN") ? 1 : 0);
    restoringHistory_ = false;
    skipNextNavigationHistory_ = false;
    updateNavigationButtons();
    refreshAll();
    visitedAnalysisPages_.insert(contentStack_->currentIndex());
    playAnalysisIntro(contentStack_->currentIndex());
}

void AdminWindow::applyAdminPermissions(const QJsonObject &admin)
{
    if (navigation_ == nullptr) return;
    const QString role = admin.value(QStringLiteral("role")).toString();
    const bool systemAdmin = role == QStringLiteral("SYS_ADMIN");
    const bool stationAdmin = role == QStringLiteral("STATION_ADMIN");
    const bool userAdmin = role == QStringLiteral("USER_ADMIN");
    currentAdminAccountsAvailable_ = admin.value(QStringLiteral("adminAccountsAvailable")).toBool();
    changePasswordButton_->setEnabled(currentAdminAccountsAvailable_);
    changePasswordButton_->setToolTip(currentAdminAccountsAvailable_ ? QString()
        : QStringLiteral("管理员管理及改密尚未启用，请联系维护人员"));
    navigation_->item(0)->setHidden(userAdmin);
    navigation_->item(1)->setHidden(false);
    navigation_->item(2)->setHidden(userAdmin);
    navigation_->item(3)->setHidden(userAdmin);
    navigation_->item(4)->setHidden(stationAdmin);
    navigation_->item(5)->setHidden(false);
    navigation_->item(6)->setHidden(!systemAdmin);
    navigation_->item(7)->setHidden(!systemAdmin || !currentAdminAccountsAvailable_);
    updateNavigationGroups(navigation_);
    auto *resourceCard = static_cast<MetricActionCard *>(resourceCount_->parentWidget());
    resourceCard->setActionPermitted(!userAdmin);
    resourceCard->setToolTip(userAdmin ? QStringLiteral("当前账号没有资产管理权限") : QStringLiteral("查看充电站或充电桩"));
    if (accountIdentity_ != nullptr) {
        accountIdentity_->setTextFormat(Qt::PlainText);
        accountIdentity_->setText(admin.value(QStringLiteral("displayName")).toString());
    }
}

bool AdminWindow::showChangePasswordDialog(bool required)
{
    if (facade_ == nullptr || currentAdminId_ <= 0) return false;
    QDialog dialog(this);
    dialog.setWindowTitle(required ? QStringLiteral("首次登录修改密码")
                                   : QStringLiteral("修改密码"));
    if (required) dialog.setWindowFlag(Qt::WindowCloseButtonHint, false);
    auto *layout = new QVBoxLayout(&dialog);
    auto *description = new QLabel(
        required ? QStringLiteral("该账号使用初始密码，请先设置新密码。修改后需要重新登录。")
                 : QStringLiteral("修改成功后将退出管理后台，请使用新密码重新登录。"),
        &dialog);
    description->setWordWrap(true);
    layout->addWidget(description);
    auto *form = new QFormLayout;
    auto *currentPassword = new QLineEdit(&dialog);
    auto *newPassword = new QLineEdit(&dialog);
    auto *confirmPassword = new QLineEdit(&dialog);
    currentPassword->setEchoMode(QLineEdit::Password);
    newPassword->setEchoMode(QLineEdit::Password);
    confirmPassword->setEchoMode(QLineEdit::Password);
    currentPassword->setPlaceholderText(QStringLiteral("当前密码"));
    newPassword->setPlaceholderText(QStringLiteral("6 至 128 个字符"));
    confirmPassword->setPlaceholderText(QStringLiteral("再次输入新密码"));
    if (required && passwordEdit_ != nullptr) currentPassword->setText(passwordEdit_->text());
    form->addRow(QStringLiteral("当前密码"), currentPassword);
    form->addRow(QStringLiteral("新密码"), newPassword);
    form->addRow(QStringLiteral("确认新密码"), confirmPassword);
    layout->addLayout(form);
    auto *buttons = new QDialogButtonBox(
        required ? QDialogButtonBox::Ok
                 : QDialogButtonBox::StandardButtons(QDialogButtonBox::Ok
                                                      | QDialogButtonBox::Cancel),
        &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);
    dialog.setMinimumWidth(440);

    while (dialog.exec() == QDialog::Accepted) {
        if (newPassword->text().size() < 6 || newPassword->text().size() > 128) {
            QMessageBox::warning(&dialog, QStringLiteral("密码不符合要求"),
                                 QStringLiteral("新密码长度必须为 6 至 128 个字符。"));
            continue;
        }
        if (newPassword->text() != confirmPassword->text()) {
            QMessageBox::warning(&dialog, QStringLiteral("密码不一致"),
                                 QStringLiteral("两次输入的新密码不一致。"));
            continue;
        }
        const ServiceResult result = facade_->changePassword(currentPassword->text(),
                                                             newPassword->text());
        if (!result.ok()) {
            showServiceError(result.code, result.message);
            continue;
        }
        currentAdminId_ = 0;
        currentAdminRole_.clear();
        currentAdminAccountsAvailable_ = false;
        supportTicketsPage_->clear();
        backHistory_.clear();
        forwardHistory_.clear();
        rootStack_->setCurrentIndex(0);
        passwordEdit_->clear();
        QMessageBox::information(this, QStringLiteral("密码已修改"),
                                 QStringLiteral("请使用新密码重新登录。"));
        return true;
    }
    return false;
}

void AdminWindow::selectPage(int index)
{
    if (index < 0 || contentStack_ == nullptr || index >= contentStack_->count()) return;
    if (currentAdminId_ > 0 && navigation_->item(index)->isHidden()) {
        const QSignalBlocker blocker(navigation_);
        navigation_->setCurrentRow(contentStack_->currentIndex());
        return;
    }
    if (refreshFeedback_ != nullptr) refreshFeedback_->hide();
    const int previousIndex = contentStack_->currentIndex();
    if (historyReady_ && !restoringHistory_ && previousIndex >= 0
        && previousIndex != index) {
        if (skipNextNavigationHistory_) {
            skipNextNavigationHistory_ = false;
        } else {
            pushNavigationHistory();
        }
    }
    static const QStringList titles{QStringLiteral("运营监控"), QStringLiteral("营收统计"),
                                    QStringLiteral("充电站管理"), QStringLiteral("充电桩管理"),
                                    QStringLiteral("用户管理"), QStringLiteral("订单管理"),
                                    QStringLiteral("客服工单"), QStringLiteral("管理员管理")};
    contentStack_->setCurrentIndex(index);
    {
        const QSignalBlocker navigationBlocker(navigation_);
        navigation_->setCurrentRow(index);
    }
    pageTitle_->setText(titles.value(index));
    if (index == 0 && operationsClock_)
        operationsClock_->setText(adminTimeText(QDateTime::currentDateTimeUtc().toString(Qt::ISODate)));
    if (currentAdminId_ <= 0) {
        updateNavigationButtons();
        return;
    }
    switch (index) {
    case 0: refreshOperations(); break;
    case 1: refreshDashboard(); break;
    case 2: refreshStations(); break;
    case 3: refreshPiles(); break;
    case 4: refreshUsers(); break;
    case 5: refreshOrders(); break;
    case 6: if (!restoringHistory_) supportTicketsPage_->refresh(); break;
    case 7: refreshAdmins(); break;
    default: break;
    }
    if (index < 2 && !restoringHistory_ && !visitedAnalysisPages_.contains(index)) {
        visitedAnalysisPages_.insert(index);
        playAnalysisIntro(index);
    }
    updateNavigationButtons();
}

void AdminWindow::navigateToAnalysisPage(const PageState &state)
{
    if (currentAdminId_<=0 || state.pageIndex<0 || state.pageIndex>=navigation_->count()
        || navigation_->item(state.pageIndex)->isHidden()) return;
    pushNavigationHistory();
    restorePageState(state);
}

void AdminWindow::openAnalysisPiles(qint64 stationId, const QSet<QString> &statuses, bool activeOnly)
{
    PageState state;
    state.pageIndex=3; state.pileStatuses=statuses; state.pilesActiveStationsOnly=activeOnly;
    if (stationId>0) state.pileStations.insert(stationId);
    navigateToAnalysisPage(state);
}

void AdminWindow::openRevenueOrders(const QDate &start, const QDate &end, qint64 stationId,
                                   const QString &stationName, const QString &mode, int startPeriod)
{
    Q_UNUSED(stationName);
    PageState state;
    state.pageIndex=5;
    state.orderStatuses.insert(QStringLiteral("COMPLETED"));
    if (start.isValid() && end.isValid()) {
        const QTimeZone zone("Asia/Shanghai");
        state.orderTimeHours=-1;
        state.orderStartTime=QDateTime(start,QTime(0,0),zone).toUTC();
        state.orderEndTime=QDateTime(end.addDays(1),QTime(0,0),zone).toUTC().addMSecs(-1);
    }
    if (stationId>0) state.orderStations.insert(stationId);
    if (!mode.isEmpty()) state.orderModes.insert(mode);
    state.orderStartPeriod=startPeriod;
    navigateToAnalysisPage(state);
}

void AdminWindow::wireAnalysisActions()
{
    const auto bind = [](QLabel *value, const QString &key, const QString &hint, std::function<void()> action) {
        auto *card = static_cast<MetricActionCard *>(value->parentWidget());
        card->setProperty("actionKey",key);
        card->setToolTip(hint);
        card->setAccessibleDescription(hint + QStringLiteral("；按回车或空格打开"));
        card->action=std::move(action);
        card->setActionPermitted(true);
        for (auto *child : card->findChildren<QWidget *>()) child->setAttribute(Qt::WA_TransparentForMouseEvents);
    };
    const auto choose = [this](QLabel *value, const QList<QPair<QString,std::function<void()>>> &actions) {
        auto *card = value->parentWidget();
        QMenu menu(card); menu.setObjectName(QStringLiteral("metricActionMenu"));
        for (const auto &action : actions) menu.addAction(action.first,this,action.second);
        menu.exec(card->mapToGlobal(QPoint(0,card->height())));
    };
    bind(operationsStations_,"operatingStations",QStringLiteral("查看运营中站点或全部站点"),[this,choose] {
        choose(operationsStations_,{
            {QStringLiteral("运营中站点"),[this] { PageState state; state.pageIndex=2; state.stationStatuses={"ACTIVE"}; navigateToAnalysisPage(state); }},
            {QStringLiteral("全部站点"),[this] { PageState state; state.pageIndex=2; navigateToAnalysisPage(state); }}
        });
    });
    bind(operationsIdle_,"availablePiles",QStringLiteral("查看运营中站点的空闲电桩"),[this] { openAnalysisPiles(0,{"IDLE"},true); });
    bind(operationsInUse_,"occupiedPiles",QStringLiteral("查看充电中、已预约或全部占用电桩"),[this,choose] {
        choose(operationsInUse_,{
            {QStringLiteral("充电中电桩"),[this] { openAnalysisPiles(0,{"CHARGING"}); }},
            {QStringLiteral("已预约电桩"),[this] { openAnalysisPiles(0,{"RESERVED"}); }},
            {QStringLiteral("全部占用电桩"),[this] { openAnalysisPiles(0,{"CHARGING","RESERVED"}); }}
        });
    });
    bind(operationsAbnormal_,"abnormalPiles",QStringLiteral("查看故障、离线或全部异常电桩"),[this,choose] {
        choose(operationsAbnormal_,{
            {QStringLiteral("故障电桩"),[this] { openAnalysisPiles(0,{"FAULT"}); }},
            {QStringLiteral("离线电桩"),[this] { openAnalysisPiles(0,{"OFFLINE"}); }},
            {QStringLiteral("全部异常电桩"),[this] { openAnalysisPiles(0,{"FAULT","OFFLINE"}); }}
        });
    });
    bind(todayRevenue_,"todayRevenue",QStringLiteral("查看今日支付的已完成订单"),[this] { openRevenueOrders(revenueSnapshotDate_,revenueSnapshotDate_); });
    bind(monthRevenue_,"monthRevenue",QStringLiteral("查看本月支付的已完成订单"),[this] {
        openRevenueOrders(QDate(revenueSnapshotDate_.year(),revenueSnapshotDate_.month(),1),
            QDate(revenueSnapshotDate_.year(),revenueSnapshotDate_.month(),revenueSnapshotDate_.daysInMonth()));
    });
    bind(totalRevenue_,"totalRevenue",QStringLiteral("查看全部已完成订单"),[this] { openRevenueOrders({},{}); });
    bind(resourceCount_,"resources",QStringLiteral("查看充电站或充电桩"),[this,choose] {
        choose(resourceCount_,{
            {QStringLiteral("查看充电站"),[this] { PageState state; state.pageIndex=2; navigateToAnalysisPage(state); }},
            {QStringLiteral("查看充电桩"),[this] { openAnalysisPiles(); }}
        });
    });
    const QList<QPair<QLabel *,QString>> rangeMetrics{{rangeRevenue_,"rangeRevenue"},{rangeOrders_,"rangeOrders"},
        {rangeEnergy_,"rangeEnergy"},{rangeAverage_,"rangeAverage"}};
    for (const auto &entry : rangeMetrics)
        bind(entry.first,entry.second,QStringLiteral("查看所选支付日期范围内的已完成订单"),[this] {
            openRevenueOrders(revenueStartDate_,revenueEndDate_);
        });
    for (auto *chart : {revenueChart_,paidOrdersChart_,energyChart_}) {
        connect(chart,&RevenueChart::dateClicked,this,[this](const QString &text) {
            const auto date=QDate::fromString(text,Qt::ISODate);
            if (date.isValid()) openRevenueOrders(date,date);
        });
    }
    connect(stationRevenueChart_,&AnalysisBarChart::barClicked,this,[this](const QString &key) {
        for (const auto &bar : stationRevenueChart_->bars())
            if (bar.key==key) { openRevenueOrders(revenueStartDate_,revenueEndDate_,key.toLongLong(),bar.label); return; }
    });
    connect(modeRevenueChart_,&PileStatusChart::statusClicked,this,[this](const QString &mode) {
        if (mode=="DIRECT" || mode=="RESERVATION") openRevenueOrders(revenueStartDate_,revenueEndDate_,0,{},mode);
    });
    connect(startPeriodChart_,&AnalysisBarChart::barClicked,this,[this](const QString &key) {
        bool valid=false; const int period=key.toInt(&valid);
        if (valid && period>=0 && period<6) openRevenueOrders(revenueStartDate_,revenueEndDate_,0,{},{},period);
    });
    connect(stationOccupancyChart_,&PileStatusChart::statusClicked,this,[this](const QString &key) {
        bool valid=false; const int band=key.toInt(&valid);
        if (!valid || band<0 || band>=5) return;
        PageState state; state.pageIndex=2; state.stationOccupancy=band; navigateToAnalysisPage(state);
    });
    connect(stationFaultChart_,&AnalysisBarChart::barClicked,this,[this](const QString &key) {
        if (key.toLongLong()>0) openAnalysisPiles(key.toLongLong(),{"FAULT","OFFLINE"});
    });
    connect(orderStatesChart_,&PileStatusChart::statusClicked,this,[this](const QString &key) {
        if (!QSet<QString>{"RESERVED","CHARGING","PENDING_PAYMENT","COMPLETED","CANCELLED"}.contains(key)) return;
        PageState state; state.pageIndex=5; state.orderStatuses={key}; navigateToAnalysisPage(state);
    });
    const auto stationCell = [this](QTableWidgetItem *item) {
        if (!item || contentStack_->currentIndex()!=0) return;
        const auto id=operationsTable_->item(item->row(),0)->data(Qt::UserRole).toLongLong();
        if (item->column()==0) {
            PageState state; state.pageIndex=2; state.selectedStationId=id; state.expandedStations={id}; navigateToAnalysisPage(state);
        } else {
            const QList<QSet<QString>> statuses{{},{"IDLE"},{"CHARGING","RESERVED"},{"OFFLINE"},{"FAULT"}};
            openAnalysisPiles(id,statuses.value(item->column()-1));
        }
    };
    connect(operationsTable_,&QTableWidget::itemClicked,this,stationCell);
    connect(operationsTable_,&QTableWidget::itemActivated,this,stationCell);
}

void AdminWindow::playAnalysisIntro(int page)
{
    if (page < 0 || page > 1 || currentAdminId_ <= 0
        || contentStack_->currentIndex() != page || !contentStack_->isVisible()) return;
    auto *content = contentStack_->widget(page);
    for (auto *label : content->findChildren<QLabel *>())
        if (auto *metric = dynamic_cast<AnimatedMetricLabel *>(label)) metric->playIntro();
    for (auto *chart : content->findChildren<RevenueChart *>()) chart->playIntro();
    for (auto *chart : content->findChildren<PileStatusChart *>()) chart->playIntro();
    for (auto *chart : content->findChildren<AnalysisBarChart *>()) chart->playIntro();
}

void AdminWindow::refreshAll()
{
    refreshDashboard();
    if (currentAdminRole_ == QStringLiteral("SYS_ADMIN")
        || currentAdminRole_ == QStringLiteral("STATION_ADMIN")) {
        refreshOperations();
        refreshStations();
        refreshPiles();
    }
    if (currentAdminRole_ == QStringLiteral("SYS_ADMIN")
        || currentAdminRole_ == QStringLiteral("USER_ADMIN")) {
        refreshUsers();
    }
    refreshOrders();
    if (currentAdminRole_ == QStringLiteral("SYS_ADMIN") && currentAdminAccountsAvailable_) refreshAdmins();
}

void AdminWindow::refreshCurrentPage()
{
    if (contentStack_ == nullptr) return;
    // This is a view refresh, not a data operation.  Reset all controls that
    // belong to the visible page while signals are blocked, then fetch that
    // page once.  In particular, do not call pushNavigationHistory(): a
    // browser refresh keeps both the back and forward stacks intact.
    switch (contentStack_->currentIndex()) {
    case 0: { // 运营监控
        refreshOperations();
        break;
    }
    case 1: { // 营收统计
        const QSignalBlocker daysBlocker(dashboardDays_);
        const QSignalBlocker startBlocker(dashboardStartDate_);
        const QSignalBlocker endBlocker(dashboardEndDate_);
        if (dashboardDays_ != nullptr) dashboardDays_->setCurrentIndex(dashboardDays_->findData(7));
        if (dashboardStartDate_ != nullptr) dashboardStartDate_->setDate(QDateTime::currentDateTimeUtc().toTimeZone(QTimeZone("Asia/Shanghai")).date().addDays(-6));
        if (dashboardEndDate_ != nullptr) dashboardEndDate_->setDate(QDateTime::currentDateTimeUtc().toTimeZone(QTimeZone("Asia/Shanghai")).date());
        dashboardCustomRange_->hide();
        if (dashboardStartLabel_ != nullptr) dashboardStartLabel_->hide();
        if (dashboardStartDate_ != nullptr) dashboardStartDate_->hide();
        if (dashboardEndLabel_ != nullptr) dashboardEndLabel_->hide();
        if (dashboardEndDate_ != nullptr) dashboardEndDate_->hide();
        if (dashboardApplyButton_ != nullptr) dashboardApplyButton_->hide();
        refreshDashboard();
        break;
    }
    case 2: { // 充电站管理
        const QSignalBlocker searchBlocker(stationSearch_);
        const QSignalBlocker fieldBlocker(stationSearchField_);
        const QSignalBlocker regionBlocker(stationRegion_);
        const QSignalBlocker statusBlocker(stationStatus_);
        if (stationSearch_ != nullptr) stationSearch_->clear();
        appliedStationSearch_.clear();
        if (stationSearchField_ != nullptr) stationSearchField_->setCurrentIndex(2);
        if (stationRegion_ != nullptr) stationRegion_->setCurrentIndex(0);
        if (stationStatus_ != nullptr) stationStatus_->setCurrentIndex(0);
        stationOccupancy_=-1;
        selectedStationRegions_.clear();
        selectedStationStatuses_.clear();
        updateFilterButton(stationRegionFilter_, 0);
        updateFilterButton(stationStatusFilter_, 0);
        if (stationClickTimer_ != nullptr) stationClickTimer_->stop();
        pendingStationClick_ = nullptr;
        pendingExpandedStations_.clear();
        restoreExpandedStationsPending_ = false;
        expandStationAfterRefresh_ = 0;
        if (stationsTable_ != nullptr) {
            for (int i = 0; i < stationsTable_->topLevelItemCount(); ++i) {
                stationsTable_->topLevelItem(i)->setExpanded(false);
            }
            stationsTable_->clearSelection();
            stationsTable_->setCurrentItem(nullptr);
        }
        if (stationExpandToggle_ != nullptr) stationExpandToggle_->setText(QStringLiteral("全部展开"));
        refreshStations();
        break;
    }
    case 3: { // 充电桩管理
        const QSignalBlocker searchBlocker(pileSearch_);
        const QSignalBlocker fieldBlocker(pileSearchField_);
        const QSignalBlocker stationBlocker(pileStation_);
        const QSignalBlocker statusBlocker(pileStatus_);
        if (pileSearch_ != nullptr) pileSearch_->clear();
        appliedPileSearch_.clear();
        if (pileSearchField_ != nullptr) pileSearchField_->setCurrentIndex(2);
        if (pileStation_ != nullptr) pileStation_->setCurrentIndex(0);
        if (pileStatus_ != nullptr) pileStatus_->setCurrentIndex(0);
        selectedPileStations_.clear();
        selectedPileStatuses_.clear();
        pilesActiveStationsOnly_=false;
        focusPileAfterRefresh_ = 0;
        updateFilterButton(pileStationFilter_, 0);
        updateFilterButton(pileStatusFilter_, 0);
        if (pilesTable_ != nullptr) {
            pilesTable_->clearSelection();
            pilesTable_->setCurrentCell(-1, -1);
        }
        refreshPiles();
        break;
    }
    case 4: { // 用户管理
        const QSignalBlocker searchBlocker(userSearch_);
        const QSignalBlocker fieldBlocker(userSearchField_);
        const QSignalBlocker statusBlocker(userStatus_);
        if (userSearch_ != nullptr) userSearch_->clear();
        appliedUserSearch_.clear();
        if (userSearchField_ != nullptr) userSearchField_->setCurrentIndex(2);
        if (userStatus_ != nullptr) userStatus_->setCurrentIndex(0);
        selectedUserStatuses_.clear();
        updateFilterButton(userStatusFilter_, 0);
        if (usersTable_ != nullptr) {
            usersTable_->clearSelection();
            usersTable_->setCurrentCell(-1, -1);
        }
        refreshUsers();
        break;
    }
    case 5: { // 订单管理
        const QSignalBlocker searchBlocker(orderSearch_);
        const QSignalBlocker fieldBlocker(orderSearchField_);
        const QSignalBlocker statusBlocker(orderStatus_);
        const QSignalBlocker modeBlocker(orderMode_);
        if (orderSearch_ != nullptr) orderSearch_->clear();
        appliedOrderSearch_.clear();
        if (orderSearchField_ != nullptr) orderSearchField_->setCurrentIndex(3);
        if (orderStatus_ != nullptr) orderStatus_->setCurrentIndex(0);
        if (orderMode_ != nullptr) orderMode_->setCurrentIndex(0);
        selectedOrderStatuses_.clear();
        selectedOrderModes_.clear();
        updateFilterButton(orderStatusFilter_, 0);
        updateFilterButton(orderModeFilter_, 0);
        if (ordersTable_ != nullptr) {
            ordersTable_->clearSelection();
            ordersTable_->setCurrentCell(-1, -1);
        }
        selectedOrderStations_.clear(); orderStartPeriod_ = -1;
        resetOrderTimeFilter();
        refreshOrders();
        break;
    }
    case 6: supportTicketsPage_->refresh(); break;
    case 7: { // 管理员管理
        const QSignalBlocker searchBlocker(adminSearch_);
        if (adminSearch_ != nullptr) adminSearch_->clear();
        appliedAdminSearch_.clear();
        selectedAdminStatuses_.clear();
        selectedAdminRoles_.clear();
        updateFilterButton(adminStatusFilter_, 0);
        updateFilterButton(adminRoleFilter_, 0);
        if (adminsTable_ != nullptr) {
            adminsTable_->clearSelection();
            adminsTable_->setCurrentCell(-1, -1);
        }
        refreshAdmins();
        break;
    }
    default:
        break;
    }
    playAnalysisIntro(contentStack_->currentIndex());
    static_cast<PageRefreshFeedback *>(refreshFeedback_)->replay();
    updateNavigationButtons();
}

void AdminWindow::refreshAdmins()
{
    if (facade_ == nullptr || adminsTable_ == nullptr
        || currentAdminRole_ != QStringLiteral("SYS_ADMIN") || !currentAdminAccountsAvailable_) return;
    const ServiceResult result = facade_->listAdmins(appliedAdminSearch_);
    if (!result.ok()) return showServiceError(result.code, result.message);
    QHash<qint64, QString> stationNames;
    const ServiceResult stations = facade_->listStations();
    if (stations.ok()) {
        for (const QJsonValue &value : stations.data.value(QStringLiteral("items")).toArray()) {
            const QJsonObject station = value.toObject();
            stationNames.insert(station.value(QStringLiteral("stationId")).toInteger(),
                                station.value(QStringLiteral("name")).toString());
        }
    }
    adminsTable_->setRowCount(0);
    const QJsonArray admins = result.data.value(QStringLiteral("items")).toArray();
    for (const QJsonValue &value : admins) {
        const QJsonObject admin = value.toObject();
        const QString status = admin.value(QStringLiteral("status")).toString();
        if (!selectedAdminStatuses_.isEmpty()
            && !selectedAdminStatuses_.contains(status)) continue;
        const QString role = admin.value(QStringLiteral("role")).toString();
        if (!selectedAdminRoles_.isEmpty() && !selectedAdminRoles_.contains(role)) continue;
        QStringList scopes;
        for (const QJsonValue &stationValue
             : admin.value(QStringLiteral("stationIds")).toArray()) {
            const qint64 stationId = stationValue.toInteger();
            scopes.append(stationNames.value(stationId,
                                             QStringLiteral("站点 %1").arg(stationId)));
        }
        const int row = adminsTable_->rowCount();
        adminsTable_->insertRow(row);
        auto *id = numberItem(admin.value(QStringLiteral("adminId")).toInteger());
        adminsTable_->setItem(row, 0, id);
        adminsTable_->setItem(row, 1, item(admin.value(QStringLiteral("username")).toString()));
        adminsTable_->setItem(row, 2, item(admin.value(QStringLiteral("displayName")).toString()));
        adminsTable_->setItem(row, 3, item(adminRoleText(role)));
        adminsTable_->setItem(row, 4, item(
            role == QStringLiteral("SYS_ADMIN") ? QStringLiteral("全部站点")
            : role == QStringLiteral("USER_ADMIN") ? QStringLiteral("—")
            : scopes.join(QStringLiteral("、"))));
        QString statusLabel = adminStatusText(status);
        if (admin.value(QStringLiteral("mustChangePassword")).toBool()) {
            statusLabel += QStringLiteral(" · 待改密");
        }
        auto *statusItem = item(statusLabel);
        statusItem->setData(Qt::UserRole, status);
        colorStatus(statusItem, status);
        adminsTable_->setItem(row, 5, statusItem);
        const QJsonValue lastLogin = admin.value(QStringLiteral("lastLoginAt"));
        adminsTable_->setItem(row, 6, item(adminTimeText(lastLogin.toString(), QStringLiteral("从未登录"))));
    }
}

void AdminWindow::showAdminDetails(qint64 adminId)
{
    const auto result = facade_->listAdmins();
    if (!result.ok()) return showServiceError(result.code, result.message);
    QJsonObject target;
    for (const auto &value : result.data.value(QStringLiteral("items")).toArray()) {
        if (value.toObject().value(QStringLiteral("adminId")).toInteger() == adminId) {
            target = value.toObject();
            break;
        }
    }
    if (target.isEmpty()) return;
    const auto stations = facade_->listStations();
    if (!stations.ok()) return showServiceError(stations.code, stations.message);
    QHash<qint64, QString> names;
    for (const auto &value : stations.data.value(QStringLiteral("items")).toArray()) {
        const auto station = value.toObject();
        names.insert(station.value(QStringLiteral("stationId")).toInteger(),
                     station.value(QStringLiteral("name")).toString());
    }
    QStringList scope;
    for (const auto &value : target.value(QStringLiteral("stationIds")).toArray())
        scope << names.value(value.toInteger(), QStringLiteral("站点 %1").arg(value.toInteger()));
    const QString role = target.value(QStringLiteral("role")).toString();
    const auto timestamp = [&target](const QString &key, const QString &fallback) {
        return adminTimeText(target.value(key).toString(), fallback);
    };
    // Keep user-supplied values on one detail row; no password material is requested or shown.
    const auto singleLine = [](QString value) { return value.replace('\n', ' ').replace('\r', ' '); };
    showDetails(QStringLiteral("管理员详情"),
        QStringLiteral("管理员 ID：%1\n登录账号：%2\n显示名：%3\n角色：%4\n站点范围：%5\n账号状态：%6\n首次登录改密：%7\n最后登录：%8\n创建时间：%9\n更新时间：%10")
            .arg(adminId).arg(singleLine(target.value(QStringLiteral("username")).toString()),
                singleLine(target.value(QStringLiteral("displayName")).toString()), adminRoleText(role),
                role == QStringLiteral("SYS_ADMIN") ? QStringLiteral("全部站点")
                    : role == QStringLiteral("USER_ADMIN") ? QStringLiteral("不涉及站点授权") : singleLine(scope.join(QStringLiteral("、"))),
                adminStatusText(target.value(QStringLiteral("status")).toString()),
                target.value(QStringLiteral("mustChangePassword")).toBool() ? QStringLiteral("待修改初始密码") : QStringLiteral("无需修改"),
                timestamp(QStringLiteral("lastLoginAt"), QStringLiteral("从未登录")),
                timestamp(QStringLiteral("createdAt"), QStringLiteral("—")),
                timestamp(QStringLiteral("updatedAt"), QStringLiteral("—"))));
}

void AdminWindow::showCreateAdminDialog()
{
    const ServiceResult stationResult = facade_->listStations();
    if (!stationResult.ok()) return showServiceError(stationResult.code, stationResult.message);
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("新增管理员"));
    dialog.setObjectName(QStringLiteral("adminAccountDialog"));
    auto *layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(24, 24, 24, 20);
    layout->setSpacing(18);
    layout->addWidget(heading(dialog.windowTitle(), &dialog, "title"));
    auto *card = panel(&dialog);
    auto *cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(22, 20, 22, 20);
    cardLayout->setSpacing(16);
    auto *form = new QFormLayout;
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    form->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    form->setHorizontalSpacing(20);
    form->setVerticalSpacing(12);
    auto *username = new QLineEdit(&dialog);
    username->setObjectName(QStringLiteral("adminUsername"));
    username->setMaxLength(32);
    auto *displayName = new QLineEdit(&dialog);
    displayName->setObjectName(QStringLiteral("adminDisplayName"));
    auto *password = new QLineEdit(&dialog);
    auto *confirm = new QLineEdit(&dialog);
    auto *role = new AdminComboBox(&dialog);
    role->setObjectName(QStringLiteral("adminRoleInput"));
    password->setObjectName(QStringLiteral("adminInitialPassword"));
    confirm->setObjectName(QStringLiteral("adminConfirmPassword"));
    password->setMaxLength(128);
    confirm->setMaxLength(128);
    password->setEchoMode(QLineEdit::Password);
    confirm->setEchoMode(QLineEdit::Password);
    username->setPlaceholderText(QStringLiteral("3 至 32 位字母、数字、点、横线或下划线"));
    password->setPlaceholderText(QStringLiteral("6 至 128 个字符"));
    role->addItem(QStringLiteral("系统管理员"), QStringLiteral("SYS_ADMIN"));
    role->addItem(QStringLiteral("站点管理员"), QStringLiteral("STATION_ADMIN"));
    role->addItem(QStringLiteral("用户管理员"), QStringLiteral("USER_ADMIN"));
    form->addRow(QStringLiteral("登录账号"), username);
    form->addRow(QStringLiteral("显示名"), displayName);
    form->addRow(QStringLiteral("初始密码"), password);
    form->addRow(QStringLiteral("确认密码"), confirm);
    form->addRow(QStringLiteral("角色"), role);
    cardLayout->addLayout(form);
    auto *scopeLabel = new QLabel(QStringLiteral("授权站点（站点管理员至少选择一个）"), &dialog);
    auto *stationList = new QListWidget(&dialog);
    stationList->setObjectName(QStringLiteral("adminScopeList"));
    stationList->setMinimumHeight(140);
    stationList->setMaximumHeight(160);
    stationList->setStyleSheet(QStringLiteral(
        "QListWidget { background:#f7f9fc; border:1px solid #dfe6f0; border-radius:6px; padding:4px; }"
        "QListWidget::item { padding:7px 9px; border-radius:4px; color:#243044; }"
        "QListWidget::item:selected { background:#e8f0ff; color:#183b70; }"
        "QListWidget:disabled { background:#f3f6fb; color:#8793a7; }"));
    stationList->setSelectionMode(QAbstractItemView::MultiSelection);
    for (const QJsonValue &value : stationResult.data.value(QStringLiteral("items")).toArray()) {
        const QJsonObject station = value.toObject();
        auto *entry = new QListWidgetItem(
            QStringLiteral("%1（ID %2）")
                .arg(station.value(QStringLiteral("name")).toString())
                .arg(station.value(QStringLiteral("stationId")).toInteger()),
            stationList);
        entry->setData(Qt::UserRole, station.value(QStringLiteral("stationId")).toInteger());
    }
    const auto updateScopeEnabled = [role, scopeLabel, stationList] {
        const bool enabled = role->currentData().toString() == QStringLiteral("STATION_ADMIN");
        scopeLabel->setEnabled(enabled);
        stationList->setEnabled(enabled);
        if (!enabled) stationList->clearSelection();
    };
    connect(role, &QComboBox::currentIndexChanged, &dialog, updateScopeEnabled);
    updateScopeEnabled();
    cardLayout->addWidget(scopeLabel);
    cardLayout->addWidget(stationList);
    layout->addWidget(card);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                                         &dialog);
    buttons->setObjectName(QStringLiteral("adminFormButtons"));
    buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("取消"));
    auto *save = buttons->button(buttons->standardButtons().testFlag(QDialogButtonBox::Ok)
        ? QDialogButtonBox::Ok : QDialogButtonBox::Save);
    save->setText(buttons->standardButtons().testFlag(QDialogButtonBox::Ok)
        ? QStringLiteral("创建管理员") : QStringLiteral("保存修改"));
    save->setProperty("primary", true);
    save->setIcon(QIcon());
    save->setMinimumWidth(120);
    save->setMinimumHeight(26);
    save->style()->unpolish(save);
    save->style()->polish(save);
    auto *cancel = buttons->button(QDialogButtonBox::Cancel);
    cancel->setIcon(QIcon());
    cancel->setMinimumWidth(90);
    cancel->setMinimumHeight(26);
    save->setDefault(true);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        if (username->text().trimmed().size() < 3
            || displayName->text().trimmed().isEmpty()
            || password->text().size() < 6 || password->text() != confirm->text()) {
            QMessageBox::warning(&dialog, QStringLiteral("输入不完整"),
                                 QStringLiteral("请检查账号、显示名和两次输入的初始密码。"));
            return;
        }
        if (role->currentData().toString() == QStringLiteral("STATION_ADMIN")
            && stationList->selectedItems().isEmpty()) {
            QMessageBox::warning(&dialog, QStringLiteral("请选择站点"),
                                 QStringLiteral("站点管理员至少需要一个授权站点。"));
            return;
        }
        dialog.accept();
    });
    layout->addWidget(buttons);
    dialog.resize(640, 630);
    if (dialog.exec() != QDialog::Accepted) return;
    QJsonArray stationIds;
    for (QListWidgetItem *entry : stationList->selectedItems()) {
        stationIds.append(entry->data(Qt::UserRole).toLongLong());
    }
    const ServiceResult result = facade_->createAdmin({
        {QStringLiteral("username"), username->text().trimmed()},
        {QStringLiteral("initialPassword"), password->text()},
        {QStringLiteral("displayName"), displayName->text().trimmed()},
        {QStringLiteral("role"), role->currentData().toString()},
        {QStringLiteral("stationIds"), stationIds},
    });
    if (!result.ok()) return showServiceError(result.code, result.message);
    refreshAdmins();
    QMessageBox::information(this, QStringLiteral("新增成功"),
                             QStringLiteral("管理员已创建，首次登录时必须修改初始密码。"));
}

void AdminWindow::showEditAdminDialog(qint64 adminId)
{
    const ServiceResult adminsResult = facade_->listAdmins();
    if (!adminsResult.ok()) return showServiceError(adminsResult.code, adminsResult.message);
    QJsonObject target;
    for (const QJsonValue &value : adminsResult.data.value(QStringLiteral("items")).toArray()) {
        if (value.toObject().value(QStringLiteral("adminId")).toInteger() == adminId) {
            target = value.toObject();
            break;
        }
    }
    if (target.isEmpty()) return;
    const ServiceResult stationResult = facade_->listStations();
    if (!stationResult.ok()) return showServiceError(stationResult.code, stationResult.message);
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("编辑管理员"));
    dialog.setObjectName(QStringLiteral("adminAccountDialog"));
    auto *layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(24, 24, 24, 20);
    layout->setSpacing(18);
    layout->addWidget(heading(dialog.windowTitle(), &dialog, "title"));
    auto *card = panel(&dialog);
    auto *cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(22, 20, 22, 20);
    cardLayout->setSpacing(16);
    auto *form = new QFormLayout;
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    form->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    form->setHorizontalSpacing(20);
    form->setVerticalSpacing(12);
    auto *username = new QLabel(target.value(QStringLiteral("username")).toString(), &dialog);
    auto *displayName = new QLineEdit(target.value(QStringLiteral("displayName")).toString(), &dialog);
    displayName->setObjectName(QStringLiteral("adminDisplayName"));
    auto *role = new AdminComboBox(&dialog);
    role->setObjectName(QStringLiteral("adminRoleInput"));
    role->addItem(QStringLiteral("系统管理员"), QStringLiteral("SYS_ADMIN"));
    role->addItem(QStringLiteral("站点管理员"), QStringLiteral("STATION_ADMIN"));
    role->addItem(QStringLiteral("用户管理员"), QStringLiteral("USER_ADMIN"));
    role->setCurrentIndex(role->findData(target.value(QStringLiteral("role")).toString()));
    auto *status = new AdminComboBox(&dialog);
    status->addItem(QStringLiteral("启用"), QStringLiteral("ACTIVE"));
    status->addItem(QStringLiteral("停用"), QStringLiteral("DISABLED"));
    status->setCurrentIndex(status->findData(target.value(QStringLiteral("status")).toString()));
    const bool editingSelf = adminId == currentAdminId_;
    role->setEnabled(!editingSelf);
    status->setEnabled(!editingSelf);
    auto *reason = new QLineEdit(&dialog);
    reason->setObjectName(QStringLiteral("adminChangeReason"));
    reason->setPlaceholderText(QStringLiteral("请填写本次变更原因"));
    form->addRow(QStringLiteral("登录账号"), username);
    form->addRow(QStringLiteral("显示名"), displayName);
    form->addRow(QStringLiteral("角色"), role);
    form->addRow(QStringLiteral("状态"), status);
    form->addRow(QStringLiteral("变更原因"), reason);
    cardLayout->addLayout(form);
    auto *scopeLabel = new QLabel(QStringLiteral("授权站点（保存后覆盖原授权）"), &dialog);
    auto *stationList = new QListWidget(&dialog);
    stationList->setObjectName(QStringLiteral("adminScopeList"));
    stationList->setMinimumHeight(140);
    stationList->setMaximumHeight(160);
    stationList->setStyleSheet(QStringLiteral(
        "QListWidget { background:#f7f9fc; border:1px solid #dfe6f0; border-radius:6px; padding:4px; }"
        "QListWidget::item { padding:7px 9px; border-radius:4px; color:#243044; }"
        "QListWidget::item:selected { background:#e8f0ff; color:#183b70; }"
        "QListWidget:disabled { background:#f3f6fb; color:#8793a7; }"));
    stationList->setSelectionMode(QAbstractItemView::MultiSelection);
    QSet<qint64> selectedIds;
    for (const QJsonValue &value : target.value(QStringLiteral("stationIds")).toArray()) {
        selectedIds.insert(value.toInteger());
    }
    for (const QJsonValue &value : stationResult.data.value(QStringLiteral("items")).toArray()) {
        const QJsonObject station = value.toObject();
        const qint64 stationId = station.value(QStringLiteral("stationId")).toInteger();
        auto *entry = new QListWidgetItem(
            QStringLiteral("%1（ID %2）")
                .arg(station.value(QStringLiteral("name")).toString()).arg(stationId),
            stationList);
        entry->setData(Qt::UserRole, stationId);
        entry->setSelected(selectedIds.contains(stationId));
    }
    const auto updateScopeEnabled = [role, scopeLabel, stationList] {
        const bool enabled = role->currentData().toString() == QStringLiteral("STATION_ADMIN");
        scopeLabel->setEnabled(enabled);
        stationList->setEnabled(enabled);
        if (!enabled) stationList->clearSelection();
    };
    connect(role, &QComboBox::currentIndexChanged, &dialog, updateScopeEnabled);
    updateScopeEnabled();
    cardLayout->addWidget(scopeLabel);
    cardLayout->addWidget(stationList);
    layout->addWidget(card);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel,
                                         &dialog);
    buttons->setObjectName(QStringLiteral("adminFormButtons"));
    buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("取消"));
    auto *save = buttons->button(buttons->standardButtons().testFlag(QDialogButtonBox::Ok)
        ? QDialogButtonBox::Ok : QDialogButtonBox::Save);
    save->setText(buttons->standardButtons().testFlag(QDialogButtonBox::Ok)
        ? QStringLiteral("创建管理员") : QStringLiteral("保存修改"));
    save->setProperty("primary", true);
    save->setIcon(QIcon());
    save->setMinimumWidth(120);
    save->setMinimumHeight(26);
    save->style()->unpolish(save);
    save->style()->polish(save);
    auto *cancel = buttons->button(QDialogButtonBox::Cancel);
    cancel->setIcon(QIcon());
    cancel->setMinimumWidth(90);
    cancel->setMinimumHeight(26);
    save->setDefault(true);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        if (displayName->text().trimmed().isEmpty() || reason->text().trimmed().isEmpty()) {
            QMessageBox::warning(&dialog, QStringLiteral("输入不完整"),
                                 QStringLiteral("显示名和变更原因不能为空。"));
            return;
        }
        if (role->currentData().toString() == QStringLiteral("STATION_ADMIN")
            && stationList->selectedItems().isEmpty()) {
            QMessageBox::warning(&dialog, QStringLiteral("请选择站点"),
                                 QStringLiteral("站点管理员至少需要一个授权站点。"));
            return;
        }
        dialog.accept();
    });
    layout->addWidget(buttons);
    dialog.resize(640, 630);
    if (dialog.exec() != QDialog::Accepted) return;
    QJsonArray stationIds;
    for (QListWidgetItem *entry : stationList->selectedItems()) {
        stationIds.append(entry->data(Qt::UserRole).toLongLong());
    }
    const ServiceResult result = facade_->updateAdmin({
        {QStringLiteral("adminId"), adminId},
        {QStringLiteral("displayName"), displayName->text().trimmed()},
        {QStringLiteral("role"), role->currentData().toString()},
        {QStringLiteral("status"), status->currentData().toString()},
        {QStringLiteral("reason"), reason->text().trimmed()},
        {QStringLiteral("stationIds"), stationIds},
    });
    if (!result.ok()) return showServiceError(result.code, result.message);
    refreshAdmins();
}

void AdminWindow::refreshDashboard()
{
    if (facade_ == nullptr || dashboardDays_ == nullptr) return;
    for (auto *label : {todayRevenue_, monthRevenue_, totalRevenue_, resourceCount_,
                         rangeRevenue_, rangeOrders_, rangeEnergy_, rangeAverage_})
        static_cast<AnimatedMetricLabel *>(label)->finishIntro();
    const auto clearAnalysis = [this](const QString &message) {
        for (auto *label : {rangeRevenue_, rangeOrders_, rangeEnergy_, rangeAverage_})
            label->setText(QStringLiteral("—"));
        paidOrdersChart_->setPoints({});
        energyChart_->setPoints({});
        stationRevenueChart_->setBars({});
        modeRevenueChart_->setSlices({});
        startPeriodChart_->setBars({});
        analysisSummary_->setText(message);
        analysisSummary_->show();
    };
    const int days = dashboardDays_->currentData().toInt();
    const QDate today = QDateTime::currentDateTimeUtc().toTimeZone(QTimeZone("Asia/Shanghai")).date();
    const ServiceResult result = days < 0
        ? facade_->getDashboard(dashboardStartDate_->date(), dashboardEndDate_->date())
        : days > 30 ? facade_->getDashboard(today.addDays(1-days), today)
                    : facade_->getDashboard(days);
    if (!result.ok()) {
        for (auto *label : {todayRevenue_, monthRevenue_, totalRevenue_, resourceCount_})
            label->setText(QStringLiteral("—"));
        revenueChart_->setPoints({});
        revenueStartDate_ = {}; revenueEndDate_ = {}; revenueSnapshotDate_ = {};
        dashboardPeriod_->clear();
        clearAnalysis(QStringLiteral("营收数据暂不可用，请检查日期范围后刷新重试"));
        return showServiceError(result.code, result.message);
    }
    const QJsonObject &data = result.data;
    todayRevenue_->setText(moneyText(data.value(QStringLiteral("todayRevenueCents")).toInteger()));
    monthRevenue_->setText(moneyText(data.value(QStringLiteral("monthRevenueCents")).toInteger()));
    totalRevenue_->setText(moneyText(data.value(QStringLiteral("totalRevenueCents")).toInteger()));
    resourceCount_->setText(QStringLiteral("%1 / %2")
        .arg(data.value(QStringLiteral("stationCount")).toInt())
        .arg(data.value(QStringLiteral("pileCount")).toInt()));
    QList<RevenuePoint> points;
    for (const QJsonValue &value : data.value(QStringLiteral("revenuePoints")).toArray()) {
        const QJsonObject point = value.toObject();
        points.append({point.value(QStringLiteral("date")).toString(),
                       point.value(QStringLiteral("revenueCents")).toInteger()});
    }
    dashboardPeriod_->setText(points.isEmpty() ? QString()
        : QStringLiteral("%1 — %2").arg(
            QDate::fromString(points.first().date, Qt::ISODate).toString(QStringLiteral("yyyy.MM.dd")),
            QDate::fromString(points.last().date, Qt::ISODate).toString(QStringLiteral("yyyy.MM.dd"))));
    revenueStartDate_ = points.isEmpty() ? QDate() : QDate::fromString(points.first().date,Qt::ISODate);
    revenueEndDate_ = points.isEmpty() ? QDate() : QDate::fromString(points.last().date,Qt::ISODate);
    revenueSnapshotDate_ = today;
    revenueChart_->setPoints(std::move(points));
    const auto orders = facade_->listOrders();
    if (!orders.ok()) {
        clearAnalysis(QStringLiteral("订单分析暂不可用，请刷新重试"));
        return;
    }
    const auto dates = data.value("revenuePoints").toArray();
    if (dates.isEmpty()) {
        clearAnalysis(QStringLiteral("当前范围暂无分析数据"));
        return;
    }
    const QDate start = QDate::fromString(dates.first().toObject().value("date").toString(), Qt::ISODate);
    const QDate end = QDate::fromString(dates.last().toObject().value("date").toString(), Qt::ISODate);
    const auto analysis = analyzeRevenue(orders.data.value("items").toArray(), start, end);
    if (!analysis.valid) {
        clearAnalysis(QStringLiteral("订单分析数据异常，请刷新重试"));
        return;
    }
    rangeRevenue_->setText(moneyText(analysis.receivedCents));
    rangeOrders_->setText(QString::number(analysis.paidOrders) + QStringLiteral(" 单"));
    rangeEnergy_->setText(QString::number(analysis.energyWh/1000.0,'f',1) + QStringLiteral(" kWh"));
    rangeAverage_->setText(analysis.paidOrders ? moneyText(qRound64(double(analysis.receivedCents)/analysis.paidOrders)) : QStringLiteral("—"));
    analysisSummary_->clear();
    analysisSummary_->hide();
    QList<RevenuePoint> orderPoints, energyPoints;
    for (auto it=analysis.dailyOrders.cbegin();it!=analysis.dailyOrders.cend();++it)
        orderPoints.append({it.key().toString(Qt::ISODate),it.value()});
    for (auto it=analysis.dailyEnergy.cbegin();it!=analysis.dailyEnergy.cend();++it)
        energyPoints.append({it.key().toString(Qt::ISODate),it.value()});
    paidOrdersChart_->setPoints(orderPoints); energyChart_->setPoints(energyPoints);
    QList<AnalysisBar> ranking;
    for (auto it=analysis.stationRevenue.cbegin();it!=analysis.stationRevenue.cend();++it)
        ranking.append({analysis.stationNames.value(it.key()),double(it.value()),moneyText(it.value()),QColor("#2f6fed"),QString::number(it.key())});
    std::sort(ranking.begin(),ranking.end(),[](const auto &a,const auto &b) { return a.value != b.value ? a.value>b.value : a.label<b.label; });
    stationRevenueChart_->setBars(ranking.mid(0,8));
    modeRevenueChart_->setSlices({
        {"DIRECT",QStringLiteral("直接充电"),analysis.modeRevenue.value("DIRECT"),QColor("#2f6fed")},
        {"RESERVATION",QStringLiteral("预约充电"),analysis.modeRevenue.value("RESERVATION"),QColor("#7463c7")}});
    QList<AnalysisBar> periods;
    for (int i=0;i<6;++i) periods.append({QStringLiteral("%1:00–%2:00").arg(i*4,2,10,QChar('0')).arg((i+1)*4,2,10,QChar('0')),
        double(analysis.startPeriods[i]),QString::number(analysis.startPeriods[i])+QStringLiteral(" 单"),QColor("#159b8d"),QString::number(i)});
    startPeriodChart_->setBars(periods);
}

void AdminWindow::refreshOperations()
{
    if (facade_ == nullptr || pileStatusChart_ == nullptr || operationsTable_ == nullptr) return;
    for (auto *label : {operationsStations_, operationsIdle_, operationsInUse_, operationsAbnormal_})
        static_cast<AnimatedMetricLabel *>(label)->finishIntro();
    const ServiceResult stationResult = facade_->listStations({}, {});
    const ServiceResult pileResult = facade_->listPiles();
    if (!stationResult.ok() || !pileResult.ok()) {
        for (auto *label : {operationsStations_, operationsIdle_, operationsInUse_, operationsAbnormal_})
            label->setText(QStringLiteral("—"));
        pileStatusChart_->setSlices({});
        orderStatesChart_->setSlices({});
        stationOccupancyChart_->setSlices({});
        stationFaultChart_->setBars({});
        operationsTable_->setRowCount(0);
        operationsSummary_->setText(QStringLiteral("运营数据暂不可用，请刷新重试"));
        operationsSummary_->show();
        const auto &failure = !stationResult.ok() ? stationResult : pileResult;
        return showServiceError(failure.code, failure.message);
    }

    struct Counters { qint64 total = 0; qint64 idle = 0; qint64 inUse = 0; qint64 offline = 0; qint64 fault = 0; };
    QHash<qint64, QString> stationNames;
    QHash<qint64, Counters> byStation;
    qint64 idle = 0;
    qint64 inUse = 0;
    qint64 offline = 0;
    qint64 fault = 0;
    qint64 charging = 0, reserved = 0, available = 0;
    QSet<qint64> activeStations;
    for (const QJsonValue &value : stationResult.data.value(QStringLiteral("items")).toArray()) {
        const QJsonObject station = value.toObject();
        const qint64 stationId = station.value(QStringLiteral("stationId")).toInteger();
        stationNames.insert(stationId, station.value(QStringLiteral("name")).toString());
        byStation.insert(stationId, Counters{});
        if (station.value("status").toString()=="ACTIVE") activeStations.insert(stationId);
    }
    for (const QJsonValue &value : pileResult.data.value(QStringLiteral("items")).toArray()) {
        const QJsonObject pile = value.toObject();
        const QString status = pile.value(QStringLiteral("status")).toString();
        Counters &counters = byStation[pile.value(QStringLiteral("stationId")).toInteger()];
        ++counters.total;
        if (status=="CHARGING") ++charging;
        if (status=="RESERVED") ++reserved;
        if (status=="IDLE" && activeStations.contains(pile.value("stationId").toInteger())) ++available;
        if (status == QStringLiteral("IDLE")) { ++idle; ++counters.idle; }
        else if (status == QStringLiteral("RESERVED") || status == QStringLiteral("CHARGING")) { ++inUse; ++counters.inUse; }
        else if (status == QStringLiteral("OFFLINE")) { ++offline; ++counters.offline; }
        else { ++fault; ++counters.fault; }
    }

    pileStatusChart_->setSlices({
        {QStringLiteral("IDLE"), QStringLiteral("空闲"), idle, QColor(QStringLiteral("#22a06b"))},
        {QStringLiteral("CHARGING"), QStringLiteral("充电中"), charging, QColor("#2f6fed")},
        {QStringLiteral("RESERVED"), QStringLiteral("已预约"), reserved, QColor("#7463c7")},
        {QStringLiteral("OFFLINE"), QStringLiteral("离线"), offline, QColor(QStringLiteral("#e58b25"))},
        {QStringLiteral("FAULT"), QStringLiteral("故障"), fault, QColor(QStringLiteral("#d64545"))},
    });

    operationsStations_->setText(QStringLiteral("%1 / %2").arg(activeStations.size()).arg(stationNames.size()));
    operationsIdle_->setText(QStringLiteral("%1 个").arg(available));
    operationsInUse_->setText(QStringLiteral("%1 / %2").arg(charging).arg(reserved));
    operationsAbnormal_->setText(QStringLiteral("%1 / %2").arg(fault).arg(offline));
    operationsSummary_->clear();
    operationsSummary_->hide();
    QList<AnalysisBar> abnormal;
    QList<qint64> occupancyCounts{0,0,0,0,0};
    for (auto it=stationNames.cbegin();it!=stationNames.cend();++it) {
        const auto c=byStation.value(it.key());
        const int band = stationOccupancyBand(c.inUse,c.total);
        if (band >= 0) ++occupancyCounts[band];
        if (c.fault+c.offline>0) abnormal.append({it.value(),double(c.fault+c.offline),
            QStringLiteral("故障 %1  离线 %2").arg(c.fault).arg(c.offline),QColor(c.fault ? "#d45252" : "#c98620"),QString::number(it.key())});
    }
    const auto byValue=[](const auto &a,const auto &b) { return a.value != b.value ? a.value>b.value : a.label<b.label; };
    std::sort(abnormal.begin(),abnormal.end(),byValue);
    QList<PileStatusSlice> occupancy;
    const QList<QColor> colors{QColor("#d45252"),QColor("#e58b25"),QColor("#2f6fed"),QColor("#159b8d"),QColor("#22a06b")};
    for (int band=0;band<5;++band) occupancy.append({QString::number(band),stationOccupancyLabel(band),occupancyCounts[band],colors[band]});
    stationOccupancyChart_->setSlices(occupancy);
    stationFaultChart_->setBars(abnormal.mid(0,8));
    const auto orders=facade_->listOrders();
    if (orders.ok()) {
        QMap<QString,qint64> states;
        for (const auto &value : orders.data.value("items").toArray()) ++states[value.toObject().value("status").toString()];
        orderStatesChart_->setSlices({{"RESERVED",QStringLiteral("预约中"),states["RESERVED"],QColor("#7463c7")},
            {"CHARGING",QStringLiteral("充电中"),states["CHARGING"],QColor("#2f6fed")},
            {"PENDING_PAYMENT",QStringLiteral("待支付"),states["PENDING_PAYMENT"],QColor("#c98620")},
            {"COMPLETED",QStringLiteral("已完成"),states["COMPLETED"],QColor("#159b8d")},
            {"CANCELLED",QStringLiteral("已取消"),states["CANCELLED"],QColor("#8897ae")}});
    } else {
        orderStatesChart_->setSlices({});
        operationsSummary_->setText(QStringLiteral("订单状态读取失败，请刷新"));
        operationsSummary_->show();
    }
    operationsTable_->setRowCount(0);
    auto stationIds = stationNames.keys();
    std::sort(stationIds.begin(),stationIds.end(),[&](qint64 a,qint64 b) {
        return stationNames[a] != stationNames[b] ? stationNames[a]<stationNames[b] : a<b;
    });
    for (const auto id : stationIds) {
        const Counters counters = byStation.value(id);
        const int row = operationsTable_->rowCount();
        operationsTable_->insertRow(row);
        operationsTable_->setItem(row, 0, item(stationNames.value(id)));
        operationsTable_->item(row,0)->setData(Qt::UserRole,id);
        operationsTable_->setItem(row, 1, item(QString::number(counters.total)));
        operationsTable_->setItem(row, 2, item(QString::number(counters.idle)));
        operationsTable_->setItem(row, 3, item(QString::number(counters.inUse)));
        operationsTable_->setItem(row, 4, item(QString::number(counters.offline)));
        operationsTable_->setItem(row, 5, item(QString::number(counters.fault)));
    }
}

void AdminWindow::refreshStations()
{
    if (facade_ == nullptr || stationsTable_ == nullptr) return;
    updateFilterButton(stationOccupancyFilter_,stationOccupancy_<0 ? 0 : 1);
    stationOccupancyFilter_->setToolTip(stationOccupancy_<0 ? QStringLiteral("按占用率区间筛选站点，无桩站点不计入分布") : stationOccupancyLabel(stationOccupancy_));
    QHash<qint64,QPair<qint64,qint64>> occupancy;
    if (stationOccupancy_ >= 0) {
        const auto piles = facade_->listPiles();
        if (!piles.ok()) { stationsTable_->clear(); return showServiceError(piles.code,piles.message); }
        for (const auto &value : piles.data.value("items").toArray()) {
            const auto pile = value.toObject();
            auto &counts = occupancy[pile.value("stationId").toInteger()];
            ++counts.second;
            if (pile.value("status").toString()=="CHARGING" || pile.value("status").toString()=="RESERVED") ++counts.first;
        }
    }
    QSet<qint64> expandedIds;
    if (restoreExpandedStationsPending_) {
        expandedIds = pendingExpandedStations_;
        restoreExpandedStationsPending_ = false;
        pendingExpandedStations_.clear();
    } else {
        for (int index = 0; index < stationsTable_->topLevelItemCount(); ++index) {
            auto *item = stationsTable_->topLevelItem(index);
            if (item->isExpanded()) expandedIds.insert(item->data(0, Qt::UserRole).toLongLong());
        }
    }
    const QString region;
    const ServiceResult result = facade_->listStations(region, {});
    if (!result.ok()) return showServiceError(result.code, result.message);
    const QJsonArray rows = result.data.value(QStringLiteral("items")).toArray();
    // Use all authorized stations, before search/status filtering, so a new
    // region becomes available without changing source code or restarting.
    QSet<QString> regions = selectedStationRegions_;
    for (const auto &value : rows) {
        const QString regionName = value.toObject().value(QStringLiteral("region")).toString();
        if (!regionName.isEmpty()) regions.insert(regionName);
    }
    QStringList regionNames = regions.values();
    regionNames.sort();
    {
        const QSignalBlocker blocker(stationRegion_);
        stationRegion_->clear();
        stationRegion_->addItem(QStringLiteral("全部区域"), QString{});
        for (const auto &name : regionNames) stationRegion_->addItem(name, name);
    }
    stationsTable_->clear();
    for (const QJsonValue &value : rows) {
        const QJsonObject station = value.toObject();
        const QString status = station.value(QStringLiteral("status")).toString();
        if (!appliedStationSearch_.isEmpty()) {
            const int field = stationSearchField_ ? stationSearchField_->currentIndex() : 0;
            const bool nameMatch = station.value(QStringLiteral("name")).toString().contains(appliedStationSearch_, Qt::CaseInsensitive);
            const bool addressMatch = station.value(QStringLiteral("address")).toString().contains(appliedStationSearch_, Qt::CaseInsensitive);
            if ((field == 0 && !nameMatch) || (field == 1 && !addressMatch) || (field == 2 && !nameMatch && !addressMatch)) continue;
        }
        if (!selectedStationStatuses_.isEmpty() && !selectedStationStatuses_.contains(status)) continue;
        if (!selectedStationRegions_.isEmpty() && !selectedStationRegions_.contains(station.value(QStringLiteral("region")).toString())) continue;
        const qint64 stationId = station.value(QStringLiteral("stationId")).toInteger();
        if (stationOccupancy_>=0) {
            const auto counts = occupancy.value(stationId);
            if (stationOccupancyBand(counts.first,counts.second)!=stationOccupancy_) continue;
        }
        auto *stationItem = new QTreeWidgetItem(stationsTable_);
        stationItem->setData(0, Qt::UserRole, stationId);
        stationItem->setText(0, QString::number(stationId));
        stationItem->setText(1, station.value(QStringLiteral("name")).toString());
        stationItem->setText(2, station.value(QStringLiteral("region")).toString());
        stationItem->setText(3, QStringLiteral("%1 / %2")
            .arg(station.value(QStringLiteral("availablePileCount")).toInteger())
            .arg(station.value(QStringLiteral("totalPileCount")).toInteger()));
        stationItem->setText(4, QStringLiteral("%1%").arg(station.value(QStringLiteral("onlineRatePercent")).toDouble(), 0, 'f', 0));
        stationItem->setText(5, QStringLiteral("¥%1/度").arg(station.value(QStringLiteral("priceCentsPerKwh")).toInteger() / 100.0, 0, 'f', 2));
        stationItem->setText(6, stationStatusText(status));
        stationItem->setTextAlignment(0, Qt::AlignRight | Qt::AlignVCenter);
        stationItem->setTextAlignment(5, Qt::AlignRight | Qt::AlignVCenter);
        stationItem->setForeground(6, status == QStringLiteral("ACTIVE") ? QColor(QStringLiteral("#15803d")) : QColor(QStringLiteral("#667085")));
        QFont stationFont = stationItem->font(0);
        stationFont = stationsTable_->font();
        stationFont.setPixelSize(14);
        stationFont.setWeight(QFont::Normal);
        for (int column = 0; column < stationsTable_->columnCount(); ++column) {
            stationItem->setFont(column, stationFont);
            stationItem->setSizeHint(column, QSize(-1, 48));
        }

        const ServiceResult pileResult = facade_->listPiles(stationId);
        if (pileResult.ok()) {
            for (const QJsonValue &pileValue : pileResult.data.value(QStringLiteral("items")).toArray()) {
                const QJsonObject pile = pileValue.toObject();
                auto *child = new QTreeWidgetItem(stationItem);
                child->setData(0, Qt::UserRole, stationId);
                child->setData(0, Qt::UserRole + 1, pile.value(QStringLiteral("pileId")).toInteger());
                child->setText(0, pile.value(QStringLiteral("pileCode")).toString());
                child->setText(1, pile.value(QStringLiteral("pileType")).toString() == QStringLiteral("FAST") ? QStringLiteral("快充") : QStringLiteral("慢充"));
                child->setText(2, QStringLiteral("%1 kW").arg(pile.value(QStringLiteral("ratedPowerKw")).toDouble(), 0, 'f', 1));
                child->setText(3, pileStatusText(pile.value(QStringLiteral("status")).toString()));
                QFont childFont = stationsTable_->font();
                childFont.setPixelSize(13);
                childFont.setWeight(QFont::Normal);
                for (int column = 0; column < stationsTable_->columnCount(); ++column) {
                    child->setFont(column, childFont);
                    child->setBackground(column, QColor(QStringLiteral("#f7f9fc")));
                    child->setForeground(column, QColor(QStringLiteral("#536176")));
                    child->setSizeHint(column, QSize(-1, 40));
                }
                child->setForeground(3, pile.value(QStringLiteral("status")).toString() == QStringLiteral("FAULT")
                                                ? QColor(QStringLiteral("#c33838"))
                                                : QColor(QStringLiteral("#536176")));
            }
        }
        stationItem->setExpanded(expandedIds.contains(stationId)
                                 || expandStationAfterRefresh_ == stationId);
    }
    expandStationAfterRefresh_ = 0;
}

void AdminWindow::refreshPiles()
{
    if (facade_ == nullptr || pilesTable_ == nullptr) return;
    pileActiveScope_->setVisible(pilesActiveStationsOnly_);
    QSet<qint64> activeStationIds;
    QHash<qint64, QString> stationNames;
    if (pileStation_ != nullptr) {
        const QVariant current = pileStation_->currentData();
        const ServiceResult stations = facade_->listStations({}, {});
        if (!stations.ok()) { pilesTable_->setRowCount(0); return showServiceError(stations.code,stations.message); }
        QSignalBlocker blocker(pileStation_);
        pileStation_->clear();
        pileStation_->addItem(QStringLiteral("全部站点"), QVariant{});
        for (const QJsonValue &value : stations.data.value(QStringLiteral("items")).toArray()) {
            const QJsonObject station = value.toObject();
            if (station.value("status").toString()=="ACTIVE") activeStationIds.insert(station.value("stationId").toInteger());
            stationNames.insert(station.value(QStringLiteral("stationId")).toInteger(),
                                station.value(QStringLiteral("name")).toString());
            pileStation_->addItem(station.value(QStringLiteral("name")).toString(), station.value(QStringLiteral("stationId")).toInteger());
        }
        const int restored = pileStation_->findData(current);
        pileStation_->setCurrentIndex(restored >= 0 ? restored : 0);
    }
    const ServiceResult result = facade_->listPiles();
    if (!result.ok()) return showServiceError(result.code, result.message);
    const QJsonArray rows = result.data.value(QStringLiteral("items")).toArray();
    pilesTable_->setRowCount(0);
    for (const QJsonValue &value : rows) {
        const QJsonObject pile = value.toObject();
        const QString status = pile.value(QStringLiteral("status")).toString();
        if (pilesActiveStationsOnly_ && !activeStationIds.contains(pile.value("stationId").toInteger())) continue;
        if (!appliedPileSearch_.isEmpty()) {
            const auto field = pileSearchField_ ? pileSearchField_->currentIndex() : 0;
            const bool matchCode = pile.value(QStringLiteral("pileCode")).toString().contains(appliedPileSearch_, Qt::CaseInsensitive);
            const bool matchStation = stationNames.value(pile.value(QStringLiteral("stationId")).toInteger()).contains(appliedPileSearch_, Qt::CaseInsensitive);
            if ((field == 0 && !matchCode) || (field == 1 && !matchStation) || (field == 2 && !matchCode && !matchStation)) continue;
        }
        if (!selectedPileStations_.isEmpty() && !selectedPileStations_.contains(pile.value(QStringLiteral("stationId")).toInteger())) continue;
        if (!selectedPileStatuses_.isEmpty() && !selectedPileStatuses_.contains(status)) continue;
        const int row = pilesTable_->rowCount();
        pilesTable_->insertRow(row);
        pilesTable_->setItem(row, 0, numberItem(pile.value(QStringLiteral("pileId")).toInteger()));
        pilesTable_->setItem(row, 1, item(pile.value(QStringLiteral("pileCode")).toString()));
        pilesTable_->setItem(row, 2, item(stationNames.value(pile.value(QStringLiteral("stationId")).toInteger(), QStringLiteral("—"))));
        pilesTable_->setItem(row, 3, item(pile.value(QStringLiteral("pileType")).toString() == QStringLiteral("FAST") ? QStringLiteral("快充") : QStringLiteral("慢充")));
        auto *statusItem = item(pileStatusText(status));
        statusItem->setData(Qt::UserRole, status);
        colorStatus(statusItem, status);
        pilesTable_->setItem(row, 4, statusItem);
        const qint64 pileId = pile.value(QStringLiteral("pileId")).toInteger();

        if (focusPileAfterRefresh_ == pileId) {
            pilesTable_->selectRow(row);
            pilesTable_->scrollToItem(pilesTable_->item(row, 1),
                                      QAbstractItemView::PositionAtCenter);
            focusPileAfterRefresh_ = 0;
        }
    }
}

void AdminWindow::navigateToTicketPile(const QString &pileCode)
{
    const auto result = facade_->listPiles();
    if (!result.ok()) return showServiceError(result.code, result.message);
    for (const auto &value : result.data.value(QStringLiteral("items")).toArray()) {
        const auto pile = value.toObject();
        if (pile.value(QStringLiteral("pileCode")).toString() == pileCode) {
            navigateToPile(pile.value(QStringLiteral("pileId")).toInteger(),
                           pile.value(QStringLiteral("stationId")).toInteger());
            return;
        }
    }
    QMessageBox::information(this, QStringLiteral("无法定位电桩"),
        QStringLiteral("当前可查看的电桩中未找到 %1，请刷新后重试。").arg(pileCode));
}

void AdminWindow::navigateToPileStation(qint64 pileId)
{
    const auto result = facade_->listPiles();
    if (!result.ok()) return showServiceError(result.code, result.message);
    for (const auto &value : result.data.value(QStringLiteral("items")).toArray()) {
        const auto pile = value.toObject();
        if (pile.value(QStringLiteral("pileId")).toInteger() != pileId) continue;
        PageState destination;
        destination.pageIndex = 2;
        destination.selectedStationId = pile.value(QStringLiteral("stationId")).toInteger();
        destination.selectedStationPileId = pileId;
        destination.expandedStations.insert(destination.selectedStationId);
        // Preserve the source view before clearing filters that might hide the target station.
        pushNavigationHistory();
        restorePageState(destination);
        return;
    }
    QMessageBox::information(this, QStringLiteral("无法定位充电站"),
                             QStringLiteral("当前电桩已不可查看，请刷新后重试。"));
}

void AdminWindow::navigateToPile(qint64 pileId, qint64 stationId)
{
    if (pileId <= 0 || stationId <= 0 || pileStation_ == nullptr
        || pileStatus_ == nullptr || pileSearch_ == nullptr) {
        return;
    }

    if (historyReady_ && !restoringHistory_) {
        pushNavigationHistory();
        skipNextNavigationHistory_ = navigation_->currentRow() != 3;
    }
    const QSignalBlocker searchBlocker(pileSearch_);
    const QSignalBlocker fieldBlocker(pileSearchField_);
    pileSearchField_->setCurrentIndex(2);
    pileSearch_->clear();
    appliedPileSearch_.clear();
    // 清除当前可能隐藏目标电桩的多选筛选，并将目标站点作为唯一站点筛选。
    pilesActiveStationsOnly_=false;
    selectedPileStatuses_.clear();
    selectedPileStations_.clear();
    selectedPileStations_.insert(stationId);
    updateFilterButton(pileStatusFilter_, 0);
    updateFilterButton(pileStationFilter_, 1);
    focusPileAfterRefresh_ = pileId;
    if (navigation_->currentRow() == 3) {
        refreshPiles();
    } else {
        navigation_->setCurrentRow(3);
    }
    if (focusPileAfterRefresh_ != 0) {
        focusPileAfterRefresh_ = 0;
        QMessageBox::information(this, QStringLiteral("电桩未找到"),
                                 QStringLiteral("目标电桩可能已被删除，请刷新后重试。"));
    }
}

void AdminWindow::refreshUsers()
{
    if (facade_ == nullptr || usersTable_ == nullptr) return;
    const ServiceResult result = facade_->listUsers();
    if (!result.ok()) return showServiceError(result.code, result.message);
    const QJsonArray rows = result.data.value(QStringLiteral("items")).toArray();
    usersTable_->setRowCount(0);
    for (const QJsonValue &value : rows) {
        const QJsonObject user = value.toObject();
        const QString status = user.value(QStringLiteral("status")).toString();
        if (!appliedUserSearch_.isEmpty()) {
            const int field = userSearchField_ ? userSearchField_->currentIndex() : 0;
            const bool phoneMatch = user.value(QStringLiteral("phone")).toString().contains(appliedUserSearch_, Qt::CaseInsensitive);
            const bool nickMatch = user.value(QStringLiteral("nickname")).toString().contains(appliedUserSearch_, Qt::CaseInsensitive);
            if ((field == 0 && !phoneMatch) || (field == 1 && !nickMatch) || (field == 2 && !phoneMatch && !nickMatch)) continue;
        }
        if (!selectedUserStatuses_.isEmpty() && !selectedUserStatuses_.contains(status)) continue;
        const int row = usersTable_->rowCount();
        usersTable_->insertRow(row);
        usersTable_->setItem(row, 0, numberItem(user.value(QStringLiteral("userId")).toInteger()));
        usersTable_->setItem(row, 1, item(user.value(QStringLiteral("phone")).toString()));
        usersTable_->setItem(row, 2, item(user.value(QStringLiteral("nickname")).toString()));
        usersTable_->setItem(row, 3, item(moneyText(user.value(QStringLiteral("balanceCents")).toInteger())));
        usersTable_->item(row,3)->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        auto *statusItem = item(userStatusText(status));
        statusItem->setData(Qt::UserRole, status);
        colorStatus(statusItem, status);
        usersTable_->setItem(row, 4, statusItem);
    }
}

void AdminWindow::updateOrderTimeFilterButton()
{
    updateFilterButton(orderTimeFilter_, orderTimeHours_ == 0 ? 0 : 1);
    const QString range = orderTimeHours_ == 0 ? QStringLiteral("全部时间")
        : orderTimeHours_ == -1 ? QStringLiteral("%1 — %2").arg(
            adminTimeText(orderStartTime_.toString(Qt::ISODate)), adminTimeText(orderEndTime_.toString(Qt::ISODate)))
        : orderTimeHours_ == 24 ? QStringLiteral("近 24 小时")
        : QStringLiteral("近 %1 天").arg(orderTimeHours_ / 24);
    orderTimeFilter_->setToolTip(QStringLiteral("支付时间：%1（北京时间）").arg(range));
}

void AdminWindow::resetOrderTimeFilter()
{
    orderTimeHours_ = 0;
    orderStartTime_ = {};
    orderEndTime_ = {};
    updateOrderTimeFilterButton();
}

void AdminWindow::showOrderTimeFilter()
{
    QDialog dialog(orderTimeFilter_);
    dialog.setObjectName(QStringLiteral("orderTimeFilterPopup"));
    dialog.setWindowFlags(Qt::Popup | Qt::FramelessWindowHint);
    const QRect available = orderTimeFilter_->screen()->availableGeometry().adjusted(8,8,-8,-8);
    dialog.setFixedWidth(qMin(340, available.width()));
    auto *layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(14,12,14,12);
    layout->setSpacing(8);
    auto *title = new QLabel(QStringLiteral("支付时间"), &dialog);
    title->setStyleSheet(QStringLiteral("font-weight:600;color:#243b64;"));
    layout->addWidget(title);
    auto *choices = new QButtonGroup(&dialog);
    choices->setExclusive(true);
    const QList<QPair<int, QString>> ranges{
        {24, QStringLiteral("近 24 小时")}, {7*24, QStringLiteral("近 7 天")},
        {30*24, QStringLiteral("近 30 天")}, {60*24, QStringLiteral("近 60 天")},
        {90*24, QStringLiteral("近 90 天")}, {-1, QStringLiteral("自定义")}
    };
    for (const auto &range : ranges) {
        auto *choice = new QRadioButton(range.second, &dialog);
        choice->setProperty("rangeHours", range.first);
        choice->setMinimumHeight(28);
        choices->addButton(choice);
        choice->setChecked(orderTimeHours_ == range.first);
        layout->addWidget(choice);
    }
    auto *custom = new QWidget(&dialog);
    auto *customLayout = new QVBoxLayout(custom);
    customLayout->setContentsMargins(0,4,0,4);
    customLayout->setSpacing(6);
    const QTimeZone zone("Asia/Shanghai");
    const auto now = QDateTime::currentDateTimeUtc();
    const auto makeTime = [&](const QString &name, const QString &caption, const QDateTime &value) {
        customLayout->addWidget(new QLabel(caption, custom));
        auto *edit = new QDateTimeEdit(custom);
        edit->setObjectName(name);
        edit->setDisplayFormat(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
        edit->setCalendarPopup(true);
        const auto local = value.toTimeZone(zone);
        edit->setDate(local.date());
        edit->setTime(local.time());
        customLayout->addWidget(edit);
        return edit;
    };
    auto *start = makeTime(QStringLiteral("orderStartTime"), QStringLiteral("起始时间（北京时间）"),
                          orderStartTime_.isValid() ? orderStartTime_ : now.addSecs(-86400));
    auto *end = makeTime(QStringLiteral("orderEndTime"), QStringLiteral("终止时间（北京时间）"),
                        orderEndTime_.isValid() ? orderEndTime_ : now);
    layout->addWidget(custom);
    custom->setVisible(orderTimeHours_ < 0);
    auto *error = new QLabel(&dialog);
    error->setObjectName(QStringLiteral("orderTimeError"));
    error->setWordWrap(true);
    error->setStyleSheet(QStringLiteral("color:#b42318;font-size:13px;"));
    error->hide();
    layout->addWidget(error);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Reset | QDialogButtonBox::Ok, &dialog);
    buttons->button(QDialogButtonBox::Reset)->setText(QStringLiteral("清除"));
    buttons->button(QDialogButtonBox::Reset)->setIcon(QIcon());
    auto *confirm = buttons->button(QDialogButtonBox::Ok);
    confirm->setText(QStringLiteral("确认"));
    confirm->setIcon(QIcon());
    confirm->setProperty("primary", true);
    confirm->style()->unpolish(confirm); confirm->style()->polish(confirm);
    confirm->setEnabled(choices->checkedButton() != nullptr);
    layout->addWidget(buttons);
    const auto positionPopup = [&] {
        dialog.adjustSize();
        QPoint position = orderTimeFilter_->mapToGlobal(QPoint(0,orderTimeFilter_->height()+4));
        if (position.y()+dialog.height() > available.bottom()+1)
            position.setY(orderTimeFilter_->mapToGlobal(QPoint()).y()-dialog.height()-4);
        position.setX(qBound(available.left(),position.x(),qMax(available.left(),available.right()-dialog.width()+1)));
        position.setY(qBound(available.top(),position.y(),qMax(available.top(),available.bottom()-dialog.height()+1)));
        dialog.move(position);
    };
    for (auto *edit : {start, end}) {
        connect(edit, &QDateTimeEdit::dateTimeChanged, &dialog, [&] {
            if (error->isVisible()) { error->hide(); positionPopup(); }
        });
    }
    connect(choices, &QButtonGroup::buttonToggled, &dialog, [&](QAbstractButton *button, bool checked) {
        if (!checked) return;
        custom->setVisible(button->property("rangeHours").toInt() < 0);
        error->hide(); confirm->setEnabled(true); positionPopup();
    });
    connect(confirm, &QPushButton::clicked, &dialog, [&] {
        if (!choices->checkedButton()) return;
        if (choices->checkedButton()->property("rangeHours").toInt() < 0
            && QDateTime(start->date(),start->time(),zone) > QDateTime(end->date(),end->time(),zone)) {
            error->setText(QStringLiteral("起始时间不能晚于终止时间"));
            error->show(); positionPopup(); return;
        }
        dialog.accept();
    });
    connect(buttons->button(QDialogButtonBox::Reset), &QPushButton::clicked, &dialog, [&] { dialog.done(2); });
    positionPopup();
    const int result = dialog.exec();
    if (result == 2) resetOrderTimeFilter();
    else if (result == QDialog::Accepted && choices->checkedButton()) {
        orderTimeHours_ = choices->checkedButton()->property("rangeHours").toInt();
        if (orderTimeHours_ < 0) {
            orderStartTime_ = QDateTime(start->date(),start->time(),zone).toUTC();
            orderEndTime_ = QDateTime(end->date(),end->time(),zone).toUTC();
        }
        updateOrderTimeFilterButton();
    } else return;
    refreshOrders();
}

void AdminWindow::refreshOrders()
{
    if (facade_ == nullptr || ordersTable_ == nullptr) return;
    updateFilterButton(orderStationFilter_,selectedOrderStations_.size());
    updateFilterButton(orderPeriodFilter_,orderStartPeriod_<0 ? 0 : 1);
    orderPeriodFilter_->setToolTip(orderStartPeriod_<0 ? QStringLiteral("按充电启动时段筛选（北京时间）")
        : QStringLiteral("充电启动时段：%1:00–%2:00（北京时间）").arg(orderStartPeriod_*4,2,10,QChar('0')).arg((orderStartPeriod_+1)*4,2,10,QChar('0')));
    const ServiceResult result = facade_->listOrders();
    if (!result.ok()) { ordersTable_->setRowCount(0); return showServiceError(result.code, result.message); }
    QList<QPair<QDateTime, QJsonObject>> rows;
    for (const auto &value : result.data.value(QStringLiteral("items")).toArray()) {
        const auto order = value.toObject();
        rows.append({QDateTime::fromString(order.value(QStringLiteral("createdAt")).toString(), Qt::ISODate), order});
    }
    std::sort(rows.begin(), rows.end(), [](const auto &left, const auto &right) {
        if (left.first.isValid() != right.first.isValid()) return left.first.isValid();
        if (left.first.isValid() && left.first != right.first) return left.first > right.first;
        return left.second.value(QStringLiteral("orderId")).toInteger()
             > right.second.value(QStringLiteral("orderId")).toInteger();
    });
    QMap<qint64,QString> stationChoices;
    for (const auto &entry : rows) stationChoices[entry.second.value("stationId").toInteger()] = entry.second.value("stationName").toString();
    for (const auto id : selectedOrderStations_)
        if (!stationChoices.contains(id)) stationChoices[id]=QStringLiteral("站点 #%1").arg(id);
    {
        const QSignalBlocker blocker(orderStation_);
        orderStation_->clear(); orderStation_->addItem(QStringLiteral("全部站点"),QVariant());
        for (auto it=stationChoices.cbegin();it!=stationChoices.cend();++it) orderStation_->addItem(it.value(),it.key());
    }
    const auto now = QDateTime::currentDateTimeUtc();
    const auto start = orderTimeHours_ > 0 ? now.addSecs(-qint64(orderTimeHours_) * 3600) : orderStartTime_;
    const auto end = orderTimeHours_ > 0 ? now : orderEndTime_;
    QHash<qint64, QString> userNames;
    QHash<qint64, QString> userPhones;
    const ServiceResult userResult = facade_->listUsers();
    if (userResult.ok()) {
        for (const QJsonValue &value : userResult.data.value(QStringLiteral("items")).toArray()) {
            const QJsonObject user = value.toObject();
            userNames.insert(user.value(QStringLiteral("userId")).toInteger(),
                             QStringLiteral("%1 · %2").arg(user.value(QStringLiteral("nickname")).toString(),
                                                          user.value(QStringLiteral("phone")).toString()));
            userPhones.insert(user.value(QStringLiteral("userId")).toInteger(), user.value(QStringLiteral("phone")).toString());
        }
    }
    ordersTable_->setRowCount(0);
    for (const auto &entry : rows) {
        const auto &order = entry.second;

        const auto paid = QDateTime::fromString(order.value("paidAt").toString(),Qt::ISODate);
        if (orderTimeHours_ != 0 && (!paid.isValid() || paid < start || paid > end)) continue;
        if (!selectedOrderStations_.isEmpty() && !selectedOrderStations_.contains(order.value("stationId").toInteger())) continue;
        if (orderStartPeriod_>=0) {
            const auto started = QDateTime::fromString(order.value("startedAt").toString(),Qt::ISODate).toTimeZone(QTimeZone("Asia/Shanghai"));
            if (!started.isValid() || started.time().hour()/4 != orderStartPeriod_) continue;
        }
        const QString status = order.value(QStringLiteral("status")).toString();
        const QString keyword = appliedOrderSearch_;
        if (!keyword.isEmpty()) {
            const int field = orderSearchField_ ? orderSearchField_->currentIndex() : 0;
            const bool mOrder = order.value(QStringLiteral("orderNo")).toString().contains(keyword, Qt::CaseInsensitive);
            const bool mPhone = userPhones.value(order.value(QStringLiteral("userId")).toInteger()).contains(keyword, Qt::CaseInsensitive);
            const bool mPile = order.value(QStringLiteral("pileCode")).toString().contains(keyword, Qt::CaseInsensitive);
            if ((field == 0 && !mOrder) || (field == 1 && !mPhone) || (field == 2 && !mPile) || (field == 3 && !mOrder && !mPhone && !mPile)) continue;
        }
        if (!selectedOrderStatuses_.isEmpty() && !selectedOrderStatuses_.contains(status)) continue;
        if (!selectedOrderModes_.isEmpty() && !selectedOrderModes_.contains(order.value(QStringLiteral("mode")).toString())) continue;
        const int row = ordersTable_->rowCount();
        ordersTable_->insertRow(row);
        ordersTable_->setItem(row, 0, numberItem(order.value(QStringLiteral("orderId")).toInteger()));
        ordersTable_->setItem(row, 1, item(order.value(QStringLiteral("orderNo")).toString()));
        const QString userDisplay = userNames.value(order.value(QStringLiteral("userId")).toInteger());
        ordersTable_->setItem(row, 2, item(userDisplay.section(QStringLiteral(" · "), -1)));
        ordersTable_->setItem(row, 3, item(order.value(QStringLiteral("stationName")).toString()));
        ordersTable_->setItem(row, 4, item(order.value(QStringLiteral("pileCode")).toString()));
        ordersTable_->item(row, 3)->setData(Qt::UserRole, order.value(QStringLiteral("stationId")).toInteger());
        ordersTable_->item(row, 4)->setData(Qt::UserRole, order.value(QStringLiteral("pileId")).toInteger());
        ordersTable_->setItem(row, 5, item(order.value(QStringLiteral("mode")).toString() == QStringLiteral("DIRECT") ? QStringLiteral("直接充电") : QStringLiteral("预约")));
        auto *statusItem = item(orderStatusText(status));
        colorStatus(statusItem, status);
        ordersTable_->setItem(row, 6, statusItem);
        ordersTable_->setItem(row, 7, item(moneyText(order.value(QStringLiteral("amountCents")).toInteger())));
        ordersTable_->item(row,7)->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        ordersTable_->setItem(row, 8, item(adminTimeText(order.value(QStringLiteral("createdAt")).toString())));
        ordersTable_->setItem(row, 9, item(adminTimeText(order.value(QStringLiteral("paidAt")).toString())));
    }
}

void AdminWindow::showCreateStationDialog()
{
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("新增充电站"));
    dialog.resize(760, 620);
    auto *dialogLayout = new QVBoxLayout(&dialog);
    auto *layout = new QFormLayout;
    auto *name = new QLineEdit(&dialog);
    auto *region = new AdminComboBox(&dialog);
    region->addItems({QStringLiteral("浑南区"), QStringLiteral("和平区"),
                      QStringLiteral("沈北新区"), QStringLiteral("铁西区")});
    auto *address = new QLineEdit(&dialog);
    auto *longitude = new QDoubleSpinBox(&dialog);
    longitude->setRange(-180.0, 180.0);
    longitude->setDecimals(6);
    longitude->setValue(123.43);
    auto *latitude = new QDoubleSpinBox(&dialog);
    latitude->setRange(-90.0, 90.0);
    latitude->setDecimals(6);
    latitude->setValue(41.71);
    auto *price = new QSpinBox(&dialog);
    price->setRange(1, 10000);
    price->setValue(135);
    auto *pileCount = new QSpinBox(&dialog);
    pileCount->setRange(0, 100);
    pileCount->setValue(0);
    layout->addRow(QStringLiteral("站点名称"), name);
    layout->addRow(QStringLiteral("区域"), region);
    layout->addRow(QStringLiteral("详细地址"), address);
    layout->addRow(QStringLiteral("经度"), longitude);
    layout->addRow(QStringLiteral("纬度"), latitude);
    layout->addRow(QStringLiteral("基础单价（分/kWh）"), price);
    layout->addRow(QStringLiteral("初始电桩数"), pileCount);
    dialogLayout->addLayout(layout);

    auto *pileLabel = new QLabel(QStringLiteral("初始电桩"), &dialog);
    pileLabel->setProperty("role", "sectionTitle");
    dialogLayout->addWidget(pileLabel);
    auto *pileTable = new QTableWidget(&dialog);
    pileTable->setColumnCount(3);
    pileTable->setHorizontalHeaderLabels({QStringLiteral("电桩编号"),
                                          QStringLiteral("类型"),
                                          QStringLiteral("额定功率（kW）")});
    pileTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    pileTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    pileTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    pileTable->verticalHeader()->setVisible(false);
    pileTable->setAlternatingRowColors(true);
    dialogLayout->addWidget(pileTable, 1);

    const auto addDefaultPileRow = [pileTable](int row) {
        pileTable->insertRow(row);
        auto *code = new QLineEdit(pileTable);
        code->setText(QStringLiteral("PILE-%1-%2")
                          .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyMMddHHmmss")))
                          .arg(row + 1, 2, 10, QLatin1Char('0')));
        auto *type = new AdminComboBox(pileTable);
        type->addItem(QStringLiteral("快充"), QStringLiteral("FAST"));
        type->addItem(QStringLiteral("慢充"), QStringLiteral("SLOW"));
        type->setCurrentIndex(row % 2);
        auto *power = new QDoubleSpinBox(pileTable);
        power->setRange(0.1, 1000.0);
        power->setDecimals(1);
        power->setValue(type->currentData().toString() == QStringLiteral("FAST") ? 60.0 : 7.0);
        power->setProperty("manuallyEdited", false);
        connect(power, qOverload<double>(&QDoubleSpinBox::valueChanged), power,
                [power](double) { power->setProperty("manuallyEdited", true); });
        connect(type, &QComboBox::currentIndexChanged, power,
                [type, power] {
                    if (!power->property("manuallyEdited").toBool()) {
                        QSignalBlocker blocker(power);
                        power->setValue(type->currentData().toString() == QStringLiteral("FAST") ? 60.0 : 7.0);
                    }
                });
        pileTable->setCellWidget(row, 0, code);
        pileTable->setCellWidget(row, 1, type);
        pileTable->setCellWidget(row, 2, power);
        pileTable->setRowHeight(row, 38);
    };
    connect(pileCount, qOverload<int>(&QSpinBox::valueChanged), &dialog,
            [pileTable, addDefaultPileRow](int count) {
                while (pileTable->rowCount() < count) addDefaultPileRow(pileTable->rowCount());
                while (pileTable->rowCount() > count) pileTable->removeRow(pileTable->rowCount() - 1);
            });
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    dialogLayout->addWidget(buttons);
    if (dialog.exec() != QDialog::Accepted) return;
    QJsonArray piles;
    QSet<QString> codes;
    for (int row = 0; row < pileTable->rowCount(); ++row) {
        auto *code = qobject_cast<QLineEdit *>(pileTable->cellWidget(row, 0));
        auto *type = qobject_cast<QComboBox *>(pileTable->cellWidget(row, 1));
        auto *power = qobject_cast<QDoubleSpinBox *>(pileTable->cellWidget(row, 2));
        const QString pileCode = code == nullptr ? QString{} : code->text().trimmed();
        const QString normalized = pileCode.toCaseFolded();
        if (pileCode.isEmpty() || codes.contains(normalized)) {
            QMessageBox::warning(this, QStringLiteral("无法创建"),
                                 pileCode.isEmpty() ? QStringLiteral("请填写每个电桩的编号。")
                                                    : QStringLiteral("电桩编号不能重复。"));
            return;
        }
        codes.insert(normalized);
        piles.append(QJsonObject{
            {QStringLiteral("pileCode"), pileCode},
            {QStringLiteral("pileType"), type->currentData().toString()},
            {QStringLiteral("ratedPowerKw"), power->value()},
        });
    }
    const ServiceResult result = facade_->createStation({
        {QStringLiteral("name"), name->text().trimmed()}, {QStringLiteral("region"), region->currentText()},
        {QStringLiteral("address"), address->text().trimmed()}, {QStringLiteral("longitude"), longitude->value()},
        {QStringLiteral("latitude"), latitude->value()}, {QStringLiteral("priceCentsPerKwh"), price->value()},
        {QStringLiteral("piles"), piles},
    });
    if (!result.ok()) return showServiceError(result.code, result.message);
    expandStationAfterRefresh_ = result.data.value(QStringLiteral("station")).toObject()
                                     .value(QStringLiteral("stationId")).toInteger();
    stationSearch_->clear();
    appliedStationSearch_.clear();
    stationRegion_->setCurrentIndex(0);
    stationStatus_->setCurrentIndex(0);
    refreshAll();
}

void AdminWindow::showEditStationDialog(qint64 stationId)
{
    const ServiceResult stationResult = facade_->listStations({}, {});
    if (!stationResult.ok()) return showServiceError(stationResult.code, stationResult.message);

    QJsonObject station;
    for (const QJsonValue &value : stationResult.data.value(QStringLiteral("items")).toArray()) {
        const QJsonObject candidate = value.toObject();
        if (candidate.value(QStringLiteral("stationId")).toInteger() == stationId) {
            station = candidate;
            break;
        }
    }
    if (station.isEmpty()) {
        return showServiceError(ErrorCode::NotFound, QStringLiteral("NOT_FOUND"));
    }

    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("编辑充电站信息"));
    dialog.setMinimumWidth(520);
    auto *layout = new QFormLayout(&dialog);
    layout->setContentsMargins(24, 22, 24, 18);
    layout->setSpacing(12);

    auto *name = new QLineEdit(station.value(QStringLiteral("name")).toString(), &dialog);
    auto *region = new AdminComboBox(&dialog);
    region->addItems({QStringLiteral("浑南区"), QStringLiteral("和平区"),
                      QStringLiteral("沈北新区"), QStringLiteral("沈河区"),
                      QStringLiteral("铁西区")});
    const int regionIndex = region->findText(station.value(QStringLiteral("region")).toString());
    if (regionIndex >= 0) region->setCurrentIndex(regionIndex);
    auto *address = new QLineEdit(station.value(QStringLiteral("address")).toString(), &dialog);
    auto *longitude = new QDoubleSpinBox(&dialog);
    longitude->setRange(-180.0, 180.0);
    longitude->setDecimals(6);
    longitude->setValue(station.value(QStringLiteral("longitude")).toDouble());
    auto *latitude = new QDoubleSpinBox(&dialog);
    latitude->setRange(-90.0, 90.0);
    latitude->setDecimals(6);
    latitude->setValue(station.value(QStringLiteral("latitude")).toDouble());
    auto *price = new QSpinBox(&dialog);
    price->setRange(1, 10000);
    price->setValue(station.value(QStringLiteral("priceCentsPerKwh")).toInt());

    layout->addRow(QStringLiteral("站点名称"), name);
    layout->addRow(QStringLiteral("区域"), region);
    layout->addRow(QStringLiteral("详细地址"), address);
    layout->addRow(QStringLiteral("经度"), longitude);
    layout->addRow(QStringLiteral("纬度"), latitude);
    layout->addRow(QStringLiteral("基础单价（分/kWh）"), price);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addRow(buttons);

    if (dialog.exec() != QDialog::Accepted) return;
    if (name->text().trimmed().isEmpty() || address->text().trimmed().isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("无法保存"),
                             QStringLiteral("站点名称和详细地址不能为空。"));
        return;
    }

    const ServiceResult result = facade_->updateStation({
        {QStringLiteral("stationId"), stationId},
        {QStringLiteral("name"), name->text().trimmed()},
        {QStringLiteral("region"), region->currentText()},
        {QStringLiteral("address"), address->text().trimmed()},
        {QStringLiteral("longitude"), longitude->value()},
        {QStringLiteral("latitude"), latitude->value()},
        {QStringLiteral("priceCentsPerKwh"), price->value()},
        {QStringLiteral("status"), station.value(QStringLiteral("status")).toString()},
    });
    if (!result.ok()) return showServiceError(result.code, result.message);
    expandStationAfterRefresh_ = stationId;
    refreshAll();
}

void AdminWindow::navigateToStationPiles(qint64 stationId)
{
    if (stationId>0) openAnalysisPiles(stationId);
}

void AdminWindow::navigateToPileStatus(const QString &statusKey)
{
    if (statusKey=="IN_USE") openAnalysisPiles(0,{"CHARGING","RESERVED"});
    else if (QSet<QString>{"IDLE","CHARGING","RESERVED","OFFLINE","FAULT"}.contains(statusKey))
        openAnalysisPiles(0,{statusKey});
}

AdminWindow::PageState AdminWindow::capturePageState() const
{
    PageState state;
    state.pageIndex = contentStack_ == nullptr ? 0 : contentStack_->currentIndex();
    if (state.pageIndex<=1) {
        if (auto *scroll = qobject_cast<QScrollArea *>(contentStack_->widget(state.pageIndex)))
            state.analysisScroll = scroll->verticalScrollBar()->value();
    }
    if (stationSearch_ != nullptr) state.stationSearch = stationSearch_->text();
    if (stationSearchField_ != nullptr) state.stationSearchField = stationSearchField_->currentIndex();
    state.stationRegions = selectedStationRegions_;
    state.stationStatuses = selectedStationStatuses_;
    state.stationOccupancy = stationOccupancy_;
    if (stationsTable_ != nullptr) {
        for (int index = 0; index < stationsTable_->topLevelItemCount(); ++index) {
            const auto *station = stationsTable_->topLevelItem(index);
            if (station->isExpanded()) state.expandedStations.insert(station->data(0, Qt::UserRole).toLongLong());
        }
        if (stationsTable_->currentItem() != nullptr) {
            const auto *current = stationsTable_->currentItem();
            state.selectedStationId = current->data(0, Qt::UserRole).toLongLong();
            if (current->parent() != nullptr)
                state.selectedStationPileId = current->data(0, Qt::UserRole + 1).toLongLong();
        }
    }
    state.selectedTicketId = supportTicketsPage_->selectedTicketId();
    if (pileSearch_ != nullptr) state.pileSearch = pileSearch_->text();
    if (pileSearchField_ != nullptr) state.pileSearchField = pileSearchField_->currentIndex();
    state.pileStations = selectedPileStations_;
    state.pileStatuses = selectedPileStatuses_;
    state.pilesActiveStationsOnly = pilesActiveStationsOnly_;
    if (pilesTable_ != nullptr && pilesTable_->currentRow() >= 0) {
        state.selectedPileId = pilesTable_->item(pilesTable_->currentRow(), 0)->data(Qt::UserRole).toLongLong();
    }
    if (userSearch_ != nullptr) state.userSearch = userSearch_->text();
    if (userSearchField_ != nullptr) state.userSearchField = userSearchField_->currentIndex();
    state.userStatuses = selectedUserStatuses_;
    if (usersTable_ != nullptr && usersTable_->currentRow() >= 0) {
        state.selectedUserId = usersTable_->item(usersTable_->currentRow(), 0)->data(Qt::UserRole).toLongLong();
    }
    if (orderSearch_ != nullptr) state.orderSearch = orderSearch_->text();
    if (orderSearchField_ != nullptr) state.orderSearchField = orderSearchField_->currentIndex();
    state.orderStatuses = selectedOrderStatuses_;
    state.orderModes = selectedOrderModes_;
    state.orderStations = selectedOrderStations_;
    state.orderStartPeriod = orderStartPeriod_;
    state.orderTimeHours = orderTimeHours_;
    state.orderStartTime = orderStartTime_;
    state.orderEndTime = orderEndTime_;
    if (ordersTable_ != nullptr && ordersTable_->currentRow() >= 0) {
        state.selectedOrderId = ordersTable_->item(ordersTable_->currentRow(), 0)->data(Qt::UserRole).toLongLong();
    }
    if (adminSearch_ != nullptr) state.adminSearch = adminSearch_->text();
    state.adminStatuses = selectedAdminStatuses_;
    state.adminRoles = selectedAdminRoles_;
    if (adminsTable_ != nullptr && adminsTable_->currentRow() >= 0) {
        state.selectedAdminId = adminsTable_->item(adminsTable_->currentRow(), 0)
                                    ->data(Qt::UserRole).toLongLong();
    }
    if (dashboardDays_ != nullptr) state.dashboardDays = dashboardDays_->currentData().toInt();
    if (dashboardStartDate_ != nullptr) state.dashboardStartDate = dashboardStartDate_->date();
    if (dashboardEndDate_ != nullptr) state.dashboardEndDate = dashboardEndDate_->date();
    return state;
}

void AdminWindow::pushNavigationHistory()
{
    if (!historyReady_ || restoringHistory_) return;
    backHistory_.append(capturePageState());
    constexpr int kMaxHistory = 50;
    if (backHistory_.size() > kMaxHistory) backHistory_.removeFirst();
    forwardHistory_.clear();
    updateNavigationButtons();
}

void AdminWindow::restorePageState(const PageState &state)
{
    if (state.pageIndex < 0 || state.pageIndex >= navigation_->count()
        || navigation_->item(state.pageIndex)->isHidden()) return;
    restoringHistory_ = true;
    if (stationClickTimer_ != nullptr) stationClickTimer_->stop();
    pendingStationClick_ = nullptr;
    // Restore only the destination page, blocking change signals until its
    // complete search/filter state is ready for a single refresh.
    const auto search = [](QLineEdit *edit, QString &applied, const QString &text) {
        const QSignalBlocker blocker(edit);
        edit->setText(text);
        applied = text.trimmed();
    };
    const auto index = [](QComboBox *combo, int value) {
        const QSignalBlocker blocker(combo);
        combo->setCurrentIndex(value);
    };
    switch (state.pageIndex) {
    case 1: {
        index(dashboardDays_, dashboardDays_->findData(state.dashboardDays));
        const QSignalBlocker startBlocker(dashboardStartDate_);
        const QSignalBlocker endBlocker(dashboardEndDate_);
        if (state.dashboardStartDate.isValid()) dashboardStartDate_->setDate(state.dashboardStartDate);
        if (state.dashboardEndDate.isValid()) dashboardEndDate_->setDate(state.dashboardEndDate);
        const bool custom = state.dashboardDays < 0;
        dashboardCustomRange_->setVisible(custom);
        dashboardStartLabel_->setVisible(custom);
        dashboardStartDate_->setVisible(custom);
        dashboardEndLabel_->setVisible(custom);
        dashboardEndDate_->setVisible(custom);
        dashboardApplyButton_->setVisible(custom);
        break;
    }
    case 2:
        search(stationSearch_, appliedStationSearch_, state.stationSearch);
        index(stationSearchField_, state.stationSearchField);
        selectedStationRegions_ = state.stationRegions;
        selectedStationStatuses_ = state.stationStatuses;
        stationOccupancy_ = state.stationOccupancy;
        updateFilterButton(stationRegionFilter_, selectedStationRegions_.size());
        updateFilterButton(stationStatusFilter_, selectedStationStatuses_.size());
        pendingExpandedStations_ = state.expandedStations;
        restoreExpandedStationsPending_ = true;
        break;
    case 3:
        search(pileSearch_, appliedPileSearch_, state.pileSearch);
        index(pileSearchField_, state.pileSearchField);
        selectedPileStations_ = state.pileStations;
        selectedPileStatuses_ = state.pileStatuses;
        pilesActiveStationsOnly_ = state.pilesActiveStationsOnly;
        updateFilterButton(pileStationFilter_, selectedPileStations_.size());
        updateFilterButton(pileStatusFilter_, selectedPileStatuses_.size());
        focusPileAfterRefresh_ = state.selectedPileId;
        break;
    case 4:
        search(userSearch_, appliedUserSearch_, state.userSearch);
        index(userSearchField_, state.userSearchField);
        selectedUserStatuses_ = state.userStatuses;
        updateFilterButton(userStatusFilter_, selectedUserStatuses_.size());
        break;
    case 5:
        search(orderSearch_, appliedOrderSearch_, state.orderSearch);
        index(orderSearchField_, state.orderSearchField);
        selectedOrderStatuses_ = state.orderStatuses;
        selectedOrderModes_ = state.orderModes;
        selectedOrderStations_ = state.orderStations;
        orderStartPeriod_ = state.orderStartPeriod;
        orderTimeHours_ = state.orderTimeHours;
        orderStartTime_ = state.orderStartTime;
        orderEndTime_ = state.orderEndTime;
        updateOrderTimeFilterButton();
        updateFilterButton(orderStatusFilter_, selectedOrderStatuses_.size());
        updateFilterButton(orderModeFilter_, selectedOrderModes_.size());
        break;
    case 7:
        search(adminSearch_, appliedAdminSearch_, state.adminSearch);
        selectedAdminStatuses_ = state.adminStatuses;
        selectedAdminRoles_ = state.adminRoles;
        updateFilterButton(adminStatusFilter_, selectedAdminStatuses_.size());
        updateFilterButton(adminRoleFilter_, selectedAdminRoles_.size());
        break;
    default: break;
    }
    // Also refresh when the history entry targets the current page with a
    // different filter; currentRowChanged would not fire in that case.
    selectPage(state.pageIndex);
    if (state.pageIndex<=1) {
        if (auto *scroll = qobject_cast<QScrollArea *>(contentStack_->widget(state.pageIndex)))
            scroll->verticalScrollBar()->setValue(state.analysisScroll);
    }
    const auto selectRow = [](QTableWidget *table, qint64 id) {
        if (id <= 0) return;
        for (int row = 0; row < table->rowCount(); ++row) {
            if (table->item(row, 0)->data(Qt::UserRole).toLongLong() == id) {
                table->selectRow(row);
                table->scrollToItem(table->item(row, 0));
                break;
            }
        }
    };
    if (state.pageIndex == 2 && state.selectedStationId > 0) {
        for (int row = 0; row < stationsTable_->topLevelItemCount(); ++row) {
            auto *station = stationsTable_->topLevelItem(row);
            if (station->data(0, Qt::UserRole).toLongLong() == state.selectedStationId) {
                auto *selected = station;
                if (state.selectedStationPileId > 0) {
                    for (int child = 0; child < station->childCount(); ++child) {
                        auto *pile = station->child(child);
                        if (pile->data(0, Qt::UserRole + 1).toLongLong() == state.selectedStationPileId) {
                            station->setExpanded(true);
                            selected = pile;
                            break;
                        }
                    }
                }
                stationsTable_->setCurrentItem(selected);
                stationsTable_->scrollToItem(selected, QAbstractItemView::PositionAtCenter);
                break;
            }
        }
    } else if (state.pageIndex == 4) selectRow(usersTable_, state.selectedUserId);
    else if (state.pageIndex == 5) selectRow(ordersTable_, state.selectedOrderId);
    else if (state.pageIndex == 6) supportTicketsPage_->restoreTicketSelection(state.selectedTicketId);
    else if (state.pageIndex == 7) selectRow(adminsTable_, state.selectedAdminId);
    restoringHistory_ = false;
    updateNavigationButtons();
}

void AdminWindow::navigateBack()
{
    if (backHistory_.isEmpty()) return;
    const PageState current = capturePageState();
    const PageState previous = backHistory_.takeLast();
    forwardHistory_.append(current);
    restorePageState(previous);
}

void AdminWindow::navigateForward()
{
    if (forwardHistory_.isEmpty()) return;
    const PageState current = capturePageState();
    const PageState next = forwardHistory_.takeLast();
    backHistory_.append(current);
    restorePageState(next);
}

void AdminWindow::updateNavigationButtons()
{
    if (backButton_ != nullptr) backButton_->setEnabled(!backHistory_.isEmpty());
    if (forwardButton_ != nullptr) forwardButton_->setEnabled(!forwardHistory_.isEmpty());
}

void AdminWindow::toggleStationStatus(qint64 stationId, bool currentlyActive)
{
    const QString action = currentlyActive ? QStringLiteral("停用") : QStringLiteral("启用");
    if (QMessageBox::question(this, action + QStringLiteral("充电站"),
                              QStringLiteral("确定要%1该充电站吗？").arg(action),
                              QMessageBox::Yes | QMessageBox::No, QMessageBox::No)
        != QMessageBox::Yes) {
        return;
    }
    const ServiceResult result = facade_->setStationStatus(
        stationId, currentlyActive ? StationStatus::Disabled : StationStatus::Active);
    if (!result.ok()) return showServiceError(result.code, result.message);
    refreshAll();
}

void AdminWindow::showCreatePileDialog(qint64 fixedStationId)
{
    const ServiceResult stationResult = facade_->listStations({}, {});
    if (!stationResult.ok()) return showServiceError(stationResult.code, stationResult.message);
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("新增电桩"));
    dialog.setMinimumWidth(420);
    auto *layout = new QFormLayout(&dialog);
    auto *station = new AdminComboBox(&dialog);
    station->addItem(QStringLiteral("请选择充电站"), QVariant{});
    for (const QJsonValue &value : stationResult.data.value(QStringLiteral("items")).toArray()) {
        const QJsonObject item = value.toObject();
        if (item.value(QStringLiteral("status")).toString() == QStringLiteral("ACTIVE")) {
            station->addItem(item.value(QStringLiteral("name")).toString(),
                             item.value(QStringLiteral("stationId")).toInteger());
        }
    }
    if (fixedStationId > 0) {
        const int index = station->findData(fixedStationId);
        if (index < 0) {
            QMessageBox::warning(this, QStringLiteral("无法创建"), QStringLiteral("该充电站不存在或已停用。"));
            return;
        }
        station->setCurrentIndex(index);
        station->setEnabled(false);
    }
    auto *code = new QLineEdit(&dialog);
    code->setPlaceholderText(QStringLiteral("例如 PILE-D-01"));
    auto *type = new AdminComboBox(&dialog);
    type->addItem(QStringLiteral("快充"), QStringLiteral("FAST"));
    type->addItem(QStringLiteral("慢充"), QStringLiteral("SLOW"));
    auto *power = new QDoubleSpinBox(&dialog);
    power->setRange(0.1, 1000.0);
    power->setDecimals(1);
    power->setValue(60.0);
    connect(type, &QComboBox::currentIndexChanged, power, [type, power] {
        power->setValue(type->currentData().toString() == QStringLiteral("FAST") ? 60.0 : 7.0);
    });
    layout->addRow(QStringLiteral("所属站点"), station);
    layout->addRow(QStringLiteral("电桩编号"), code);
    layout->addRow(QStringLiteral("类型"), type);
    layout->addRow(QStringLiteral("额定功率（kW）"), power);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addRow(buttons);
    if (dialog.exec() != QDialog::Accepted) return;
    if (station->currentIndex() == 0) {
        QMessageBox::warning(this, QStringLiteral("无法创建"), QStringLiteral("请选择有效的充电站。"));
        return;
    }
    const ServiceResult result = facade_->createPile({
        {QStringLiteral("stationId"), station->currentData().toLongLong()},
        {QStringLiteral("pileCode"), code->text().trimmed()},
        {QStringLiteral("pileType"), type->currentData().toString()},
        {QStringLiteral("ratedPowerKw"), power->value()},
    });
    if (!result.ok()) return showServiceError(result.code, result.message);
    expandStationAfterRefresh_ = station->currentData().toLongLong();
    refreshAll();
}

void AdminWindow::showEditPileDialog(qint64 pileId)
{
    if (pileId <= 0) return;
    const ServiceResult result = facade_->listPiles();
    if (!result.ok()) return showServiceError(result.code, result.message);

    QJsonObject pile;
    for (const QJsonValue &value : result.data.value(QStringLiteral("items")).toArray()) {
        const QJsonObject candidate = value.toObject();
        if (candidate.value(QStringLiteral("pileId")).toInteger() == pileId) {
            pile = candidate;
            break;
        }
    }
    if (pile.isEmpty()) return showServiceError(ErrorCode::NotFound, QStringLiteral("NOT_FOUND"));

    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("修改电桩信息"));
    dialog.setMinimumWidth(500);
    auto *layout = new QFormLayout(&dialog);
    layout->setContentsMargins(26, 24, 26, 20);
    layout->setSpacing(12);

    auto *id = new QLabel(QString::number(pileId), &dialog);
    auto *station = new QLabel(QStringLiteral("站点 ID %1")
                                   .arg(pile.value(QStringLiteral("stationId")).toInteger()), &dialog);
    auto *status = new QLabel(pileStatusText(pile.value(QStringLiteral("status")).toString()), &dialog);
    status->setStyleSheet(QStringLiteral("color:#536176;"));
    auto *code = new QLineEdit(pile.value(QStringLiteral("pileCode")).toString(), &dialog);
    code->setMaxLength(64);
    auto *type = new AdminComboBox(&dialog);
    type->addItem(QStringLiteral("快充"), QStringLiteral("FAST"));
    type->addItem(QStringLiteral("慢充"), QStringLiteral("SLOW"));
    type->setCurrentIndex(pile.value(QStringLiteral("pileType")).toString() == QStringLiteral("FAST") ? 0 : 1);
    auto *power = new QDoubleSpinBox(&dialog);
    power->setRange(0.1, 1000.0);
    power->setDecimals(1);
    power->setSuffix(QStringLiteral(" kW"));
    power->setValue(pile.value(QStringLiteral("ratedPowerKw")).toDouble());

    layout->addRow(QStringLiteral("电桩 ID"), id);
    layout->addRow(QStringLiteral("所属站点"), station);
    layout->addRow(QStringLiteral("当前状态"), status);
    layout->addRow(QStringLiteral("电桩编号"), code);
    layout->addRow(QStringLiteral("类型"), type);
    layout->addRow(QStringLiteral("额定功率"), power);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addRow(buttons);

    if (dialog.exec() != QDialog::Accepted) return;
    const QString pileCode = code->text().trimmed();
    if (pileCode.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("无法保存"), QStringLiteral("电桩编号不能为空。"));
        return;
    }
    const ServiceResult update = facade_->updatePile({
        {QStringLiteral("pileId"), pileId},
        {QStringLiteral("pileCode"), pileCode},
        {QStringLiteral("pileType"), type->currentData().toString()},
        {QStringLiteral("ratedPowerKw"), power->value()},
    });
    if (!update.ok()) return showServiceError(update.code, update.message);
    refreshAll();
}

void AdminWindow::showStationDetails(qint64 stationId)
{
    const ServiceResult stationResult = facade_->listStations({}, {});
    const ServiceResult pileResult = facade_->listPiles(stationId);
    if (!stationResult.ok()) return showServiceError(stationResult.code, stationResult.message);
    if (!pileResult.ok()) return showServiceError(pileResult.code, pileResult.message);
    QJsonObject station;
    for (const QJsonValue &value : stationResult.data.value(QStringLiteral("items")).toArray()) {
        if (value.toObject().value(QStringLiteral("stationId")).toInteger() == stationId) station = value.toObject();
    }
    if (station.isEmpty()) return;
    QString text = QStringLiteral("站点：%1\nID：%2\n区域：%3\n地址：%4\n坐标：%5, %6\n基础电价：¥%7/度\n状态：%8\n可用/总数：%9/%10\n在线率：%11%\n\n所属电桩：")
        .arg(station.value(QStringLiteral("name")).toString()).arg(stationId)
        .arg(station.value(QStringLiteral("region")).toString()).arg(station.value(QStringLiteral("address")).toString())
        .arg(station.value(QStringLiteral("longitude")).toDouble(), 0, 'f', 6)
        .arg(station.value(QStringLiteral("latitude")).toDouble(), 0, 'f', 6)
        .arg(station.value(QStringLiteral("priceCentsPerKwh")).toInteger() / 100.0, 0, 'f', 2)
        .arg(stationStatusText(station.value(QStringLiteral("status")).toString()))
        .arg(station.value(QStringLiteral("availablePileCount")).toInteger())
        .arg(station.value(QStringLiteral("totalPileCount")).toInteger())
        .arg(station.value(QStringLiteral("onlineRatePercent")).toDouble(), 0, 'f', 0);
    for (const QJsonValue &value : pileResult.data.value(QStringLiteral("items")).toArray()) {
        const QJsonObject pile = value.toObject();
        text += QStringLiteral("\n• %1\t%2\t%3 kW\t%4")
            .arg(pile.value(QStringLiteral("pileCode")).toString())
            .arg(pile.value(QStringLiteral("pileType")).toString() == QStringLiteral("FAST") ? QStringLiteral("快充") : QStringLiteral("慢充"))
            .arg(pile.value(QStringLiteral("ratedPowerKw")).toDouble(), 0, 'f', 1)
            .arg(pileStatusText(pile.value(QStringLiteral("status")).toString()));
    }
    showDetails(QStringLiteral("站点详情"), text);
}

void AdminWindow::showPileDetails(qint64 pileId)
{
    const ServiceResult result = facade_->listPiles();
    if (!result.ok()) return showServiceError(result.code, result.message);
    for (const QJsonValue &value : result.data.value(QStringLiteral("items")).toArray()) {
        const QJsonObject pile = value.toObject();
        if (pile.value(QStringLiteral("pileId")).toInteger() != pileId) continue;
        showDetails(QStringLiteral("电桩详情"),
            QStringLiteral("编号：%1\nID：%2\n所属站点 ID：%3\n类型：%4\n额定功率：%5 kW\n状态：%6\n累计充电：%7 次\n累计时长：%8 小时")
                .arg(pile.value(QStringLiteral("pileCode")).toString()).arg(pileId)
                .arg(pile.value(QStringLiteral("stationId")).toInteger())
                .arg(pile.value(QStringLiteral("pileType")).toString() == QStringLiteral("FAST") ? QStringLiteral("快充") : QStringLiteral("慢充"))
                .arg(pile.value(QStringLiteral("ratedPowerKw")).toDouble(), 0, 'f', 1)
                .arg(pileStatusText(pile.value(QStringLiteral("status")).toString()))
                .arg(pile.value(QStringLiteral("chargeCount")).toInteger())
                .arg(pile.value(QStringLiteral("totalChargeSeconds")).toInteger() / 3600.0, 0, 'f', 1));
        return;
    }
}

void AdminWindow::showUserDetails(qint64 userId)
{
    const ServiceResult users = facade_->listUsers();
    const ServiceResult orders = facade_->listOrders();
    if (!users.ok()) return showServiceError(users.code, users.message);
    if (!orders.ok()) return showServiceError(orders.code, orders.message);
    QJsonObject user;
    for (const QJsonValue &value : users.data.value(QStringLiteral("items")).toArray()) {
        if (value.toObject().value(QStringLiteral("userId")).toInteger() == userId) user = value.toObject();
    }
    if (user.isEmpty()) return;
    int count = 0;
    qint64 spent = 0;
    for (const QJsonValue &value : orders.data.value(QStringLiteral("items")).toArray()) {
        const QJsonObject order = value.toObject();
        if (order.value(QStringLiteral("userId")).toInteger() == userId) {
            ++count;
            if (order.value(QStringLiteral("status")).toString() == QStringLiteral("COMPLETED")) spent += order.value(QStringLiteral("amountCents")).toInteger();
        }
    }
    showDetails(QStringLiteral("用户详情"),
        QStringLiteral("手机号：%1\n昵称：%2\n状态：%3\n余额：%4\n注册时间：%5\n用户 ID：%6\n\n订单总数：%7\n累计消费：%8")
            .arg(user.value(QStringLiteral("phone")).toString())
            .arg(user.value(QStringLiteral("nickname")).toString())
            .arg(userStatusText(user.value(QStringLiteral("status")).toString()))
            .arg(moneyText(user.value(QStringLiteral("balanceCents")).toInteger()))
            .arg(adminTimeText(user.value(QStringLiteral("createdAt")).toString())).arg(userId).arg(count).arg(moneyText(spent)));
}

void AdminWindow::showOrderDetails(qint64 orderId)
{
    const ServiceResult result = facade_->listOrders();
    if (!result.ok()) return showServiceError(result.code, result.message);
    for (const QJsonValue &value : result.data.value(QStringLiteral("items")).toArray()) {
        const QJsonObject order = value.toObject();
        if (order.value(QStringLiteral("orderId")).toInteger() != orderId) continue;
        const auto timeText = [&order](const char *key) {
            const QJsonValue value = order.value(QLatin1String(key));
            return adminTimeText(value.toString());
        };
        const QJsonValue unitPrice = order.value(QStringLiteral("unitPriceCentsPerKwh"));
        showDetails(QStringLiteral("订单详情"),
            QStringLiteral("订单号：%1\n状态：%2\n用户 ID：%3\n站点：%4（ID %5）\n电桩：%6（ID %7）\n模式：%8\n\n创建：%9\n预约：%10\n开始：%11\n结束：%12\n支付：%13\n\n时长：%14 分钟\n电量：%15 kWh\n价格快照：%16\n金额：%17")
                .arg(order.value(QStringLiteral("orderNo")).toString())
                .arg(orderStatusText(order.value(QStringLiteral("status")).toString()))
                .arg(order.value(QStringLiteral("userId")).toInteger())
                .arg(order.value(QStringLiteral("stationName")).toString())
                .arg(order.value(QStringLiteral("stationId")).toInteger())
                .arg(order.value(QStringLiteral("pileCode")).toString())
                .arg(order.value(QStringLiteral("pileId")).toInteger())
                .arg(order.value(QStringLiteral("mode")).toString() == QStringLiteral("DIRECT") ? QStringLiteral("直接充电") : QStringLiteral("预约"))
                .arg(timeText("createdAt")).arg(timeText("reservedAt")).arg(timeText("startedAt"))
                .arg(timeText("endedAt")).arg(timeText("paidAt"))
                .arg(order.value(QStringLiteral("durationSeconds")).toInteger() / 60)
                .arg(order.value(QStringLiteral("energyWh")).toInteger() / 1000.0, 0, 'f', 2)
                .arg(unitPrice.isDouble() ? QStringLiteral("¥%1/度").arg(unitPrice.toInteger() / 100.0, 0, 'f', 2) : QStringLiteral("—"))
                .arg(moneyText(order.value(QStringLiteral("amountCents")).toInteger())));
        return;
    }
}

void AdminWindow::deleteSelectedStation()
{
    QTreeWidgetItem *selected = stationsTable_->currentItem();
    if (selected == nullptr) {
        QMessageBox::information(this, QStringLiteral("删除站点"),
                                 QStringLiteral("请先选择一个充电站。"));
        return;
    }
    if (selected->parent() != nullptr) selected = selected->parent();
    const qint64 stationId = selected->data(0, Qt::UserRole).toLongLong();
    const QString stationName = selected->text(0);
    const auto answer = QMessageBox::question(
        this, QStringLiteral("删除站点"),
        QStringLiteral("确定删除“%1”及其所有无订单电桩吗？\n"
                       "已存在历史或进行中订单的站点不允许删除。")
            .arg(stationName),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer != QMessageBox::Yes) {
        return;
    }

    const ServiceResult result = facade_->deleteStation(stationId);
    if (!result.ok()) {
        if (result.code == ErrorCode::IllegalOrderState) {
            QMessageBox::warning(
                this, QStringLiteral("无法删除"),
                QStringLiteral("该站点已有历史或进行中订单，"
                               "为保留订单数据不能物理删除。"));
            return;
        }
        return showServiceError(result.code, result.message);
    }

    refreshAll();
    QMessageBox::information(this, QStringLiteral("删除站点"),
                             QStringLiteral("站点及其无订单电桩已删除。"));
}

void AdminWindow::restartSelectedPile()
{
    const int row = pilesTable_->currentRow();
    if (row < 0) {
        QMessageBox::information(this, QStringLiteral("重启电桩"), QStringLiteral("请先选择一个电桩。"));
        return;
    }
    const qint64 pileId = pilesTable_->item(row, 0)->data(Qt::UserRole).toLongLong();
    const ServiceResult result = facade_->restartPile(pileId);
    if (!result.ok()) return showServiceError(result.code, result.message);
    refreshAll();
    QMessageBox::information(this, QStringLiteral("重启电桩"), QStringLiteral("电桩已恢复为空闲状态。"));
}

void AdminWindow::deleteSelectedPile()
{
    const int row = pilesTable_->currentRow();
    if (row < 0) {
        QMessageBox::information(this, QStringLiteral("删除电桩"), QStringLiteral("请先选择一个电桩。"));
        return;
    }
    const qint64 pileId = pilesTable_->item(row, 0)->data(Qt::UserRole).toLongLong();
    const QString code = pilesTable_->item(row, 1)->text();
    if (QMessageBox::question(this, QStringLiteral("删除电桩"),
            QStringLiteral("确定删除电桩“%1”吗？\n存在订单或正在使用时不能删除。").arg(code),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) return;
    const ServiceResult result = facade_->deletePile(pileId);
    if (!result.ok()) return showServiceError(result.code, result.message);
    refreshAll();
}

void AdminWindow::toggleSelectedUserStatus()
{
    const int row = usersTable_->currentRow();
    if (row < 0) {
        QMessageBox::information(this, QStringLiteral("用户状态"), QStringLiteral("请先选择一个用户。"));
        return;
    }
    const qint64 userId = usersTable_->item(row, 0)->data(Qt::UserRole).toLongLong();
    const QString current = usersTable_->item(row, 4)->data(Qt::UserRole).toString();
    const UserStatus target = current == QStringLiteral("ACTIVE") ? UserStatus::Frozen : UserStatus::Active;
    const ServiceResult result = facade_->setUserStatus(userId, target);
    if (!result.ok()) return showServiceError(result.code, result.message);
    refreshUsers();
}

void AdminWindow::showServiceError(int code, const QString &message)
{
    qWarning().noquote() << QStringLiteral("Administrator operation failed: %1 %2")
                                .arg(code).arg(message);
    Q_UNUSED(code)
    QString detail = message;
    if (code == ErrorCode::CurrentOrderExists) detail = QStringLiteral("该用户存在未结束订单，暂时不能冻结。");
    else if (message == QStringLiteral("STATION_HAS_ACTIVE_PILES")) {
        detail = QStringLiteral("该充电站有已预约或正在充电的电桩，结束相关订单后才能停用。");
    } else if (code == ErrorCode::IllegalOrderState) detail = QStringLiteral("充电中或已预约的电桩不能重启。");
    else if (code == ErrorCode::InvalidRequest) detail = QStringLiteral("输入内容不完整或格式不正确。");
    if (message == QStringLiteral("INVALID_STATION")) detail = QStringLiteral("请选择一个已启用的充电站。");
    else if (message == QStringLiteral("PILE_CODE_EXISTS")) detail = QStringLiteral("电桩编号已存在，请使用其他编号。");
    else if (message == QStringLiteral("DUPLICATE_USERNAME")) detail = QStringLiteral("管理员账号已存在，请使用其他账号。");
    else if (message == QStringLiteral("CANNOT_DISABLE_SELF")) detail = QStringLiteral("当前登录的管理员不能停用自己。");
    else if (message == QStringLiteral("CANNOT_CHANGE_OWN_ROLE")) detail = QStringLiteral("当前登录的管理员不能修改自己的角色。");
    else if (message == QStringLiteral("LAST_SYS_ADMIN")) detail = QStringLiteral("必须至少保留一名启用状态的系统管理员。");
    else if (message == QStringLiteral("ADMIN_NOT_FOUND")) detail = QStringLiteral("目标管理员不存在或已被删除。");
    else if (message == QStringLiteral("PRINCIPAL_DISABLED")) detail = QStringLiteral("管理员账号已停用。");
    else if (message == QStringLiteral("INVALID_CREDENTIALS")) detail = QStringLiteral("当前密码错误。");
    else if (message == QStringLiteral("PASSWORD_CHANGE_REQUIRED")) detail = QStringLiteral("请先修改初始密码。");
    else if (message == QStringLiteral("ADMIN_ACCOUNTS_MIGRATION_REQUIRED")) detail = QStringLiteral("管理员管理及改密尚未启用，请联系维护人员。");
    else if (message == QStringLiteral("ROLE_FORBIDDEN")) detail = QStringLiteral("当前角色没有执行该操作的权限。");
    else if (message == QStringLiteral("STATION_SCOPE_FORBIDDEN")) detail = QStringLiteral("该充电站不在当前管理员的授权范围内。");
    QMessageBox::warning(this, QStringLiteral("操作失败"), detail);
}

void AdminWindow::prepareTable(QTableWidget *table, const QStringList &headers)
{
    table->setColumnCount(headers.size());
    table->setHorizontalHeaderLabels(headers);
    for (int column=0;column<headers.size();++column) {
        if (headers[column].contains("ID") || headers[column]==QStringLiteral("金额") || headers[column]==QStringLiteral("余额"))
            table->horizontalHeaderItem(column)->setTextAlignment(Qt::AlignRight|Qt::AlignVCenter);
    }
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    table->horizontalHeader()->setStretchLastSection(false);
    if (!headers.isEmpty()) {
        table->horizontalHeader()->setSectionResizeMode(headers.size() - 1, QHeaderView::Fixed);
        table->setColumnWidth(headers.size() - 1, 132);
    }
    table->verticalHeader()->setVisible(false);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->setAlternatingRowColors(true);
    table->setShowGrid(false);
    table->setMouseTracking(true);
    table->horizontalHeader()->setFixedHeight(44);
    table->setWordWrap(false);
    table->horizontalHeader()->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    table->horizontalHeader()->setMinimumSectionSize(90);
    table->horizontalHeader()->setStretchLastSection(false);
}

QString AdminWindow::moneyText(qint64 cents)
{
    return QStringLiteral("¥ %1").arg(cents / 100.0, 0, 'f', 2);
}

}  // namespace charging::server
