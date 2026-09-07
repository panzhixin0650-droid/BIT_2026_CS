#include "admin_window.h"

#include "admin_facade.h"
#include "pile_status_chart.h"
#include "revenue_chart.h"

#include "charging/protocol/dto.h"
#include "charging/protocol/protocol_constants.h"

#include <QComboBox>
#include <QCheckBox>
#include <QApplication>
#include <QClipboard>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDateEdit>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QEventLoop>
#include <QFrame>
#include <QGraphicsBlurEffect>
#include <QGraphicsPixmapItem>
#include <QGraphicsScene>
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
#include <QPaintEvent>
#include <QSet>
#include <QSignalBlocker>
#include <QScrollArea>
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

// Paint the artwork and scrim as the form's parent background. A live blur
// effect on an overlapping sibling can repaint over form rows during partial
// updates. Cache the effect offscreen so it never participates in widget
// repaint propagation.
class LoginPage final : public QWidget {
public:
    explicit LoginPage(QWidget *parent = nullptr)
        : QWidget(parent)
        , artwork_(QStringLiteral(":/server/login-charging-station-blue.png"))
    {
        setAttribute(Qt::WA_OpaquePaintEvent, true);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        const qreal ratio = devicePixelRatioF();
        const QSize pixelSize = size() * ratio;
        if (background_.size() != pixelSize || background_.devicePixelRatio() != ratio) {
            rebuildBackground(pixelSize, ratio);
        }
        QPainter painter(this);
        painter.drawPixmap(QPoint(0, 0), background_);
    }

private:
    void rebuildBackground(const QSize &pixelSize, qreal ratio)
    {
        background_ = QPixmap(pixelSize);
        background_.fill(QColor(QStringLiteral("#0b1f3a")));
        {
            QPainter painter(&background_);
            if (!artwork_.isNull()) {
                const QPixmap scaled = artwork_.scaled(
                    pixelSize, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
                QGraphicsScene scene;
                auto *image = scene.addPixmap(scaled);
                image->setPos((pixelSize.width() - scaled.width()) / 2.0,
                              (pixelSize.height() - scaled.height()) / 2.0);
                auto *blur = new QGraphicsBlurEffect;
                blur->setBlurRadius(12.0 * ratio);
                blur->setBlurHints(QGraphicsBlurEffect::QualityHint);
                image->setGraphicsEffect(blur);
                const QRectF area(QPointF(0, 0), QSizeF(pixelSize));
                scene.render(&painter, area, area);
            }
            painter.fillRect(background_.rect(), QColor(7, 25, 51, 46));
        }
        background_.setDevicePixelRatio(ratio);
    }

    QPixmap artwork_;
    QPixmap background_;
};

QFrame *panel(QWidget *parent)
{
    auto *frame = new QFrame(parent);
    frame->setObjectName(QStringLiteral("panel"));
    return frame;
}

QWidget *metricCard(const QString &title,
                    const QString &accent,
                    QLabel **valueLabel,
                    QWidget *parent)
{
    auto *card = panel(parent);
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
    *valueLabel = new QLabel(QStringLiteral("--"), card);
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
    return result;
}

QString userStatusText(const QString &status)
{
    return status == QStringLiteral("ACTIVE") ? QStringLiteral("正常")
                                               : QStringLiteral("已冻结");
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
    if (status == QStringLiteral("ACTIVE") || status == QStringLiteral("IDLE")
        || status == QStringLiteral("COMPLETED")) {
        tableItem->setForeground(QColor(QStringLiteral("#15803d")));
    } else if (status == QStringLiteral("CHARGING")
               || status == QStringLiteral("RESERVED")
               || status == QStringLiteral("PENDING_PAYMENT")) {
        tableItem->setForeground(QColor(QStringLiteral("#c26908")));
    } else {
        tableItem->setForeground(QColor(QStringLiteral("#c33838")));
    }
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
        dialog.setMinimumWidth(qMax(190, button->width() + 55));
        auto *layout = new QVBoxLayout(&dialog);
        layout->setContentsMargins(12, 10, 12, 10);
        layout->setSpacing(6);
        auto *title = new QLabel(button->property("filterTitle").toString(), &dialog);
        title->setStyleSheet(QStringLiteral("font-weight:600;color:#243b64;"));
        layout->addWidget(title);
        auto *all = new QCheckBox(QStringLiteral("全选"), &dialog);
        layout->addWidget(all);
        QVector<QCheckBox *> boxes;
        for (int i = 1; i < source->count(); ++i) {
            auto *box = new QCheckBox(source->itemText(i), &dialog);
            box->setChecked(isSelected(source->itemData(i)));
            boxes.append(box); layout->addWidget(box);
        }
        auto syncAll = [all, &boxes] {
            bool every = !boxes.isEmpty();
            for (auto *box : boxes) every = every && box->isChecked();
            QSignalBlocker blocker(all); all->setChecked(every);
        };
        for (auto *box : boxes) QObject::connect(box, &QCheckBox::toggled, &dialog, syncAll);
        QObject::connect(all, &QCheckBox::toggled, &dialog, [&boxes](bool checked){ for (auto *box : boxes) box->setChecked(checked); });
        syncAll();
        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok, &dialog);
        layout->addWidget(buttons);
        QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        const QPoint popupPos = button->mapToGlobal(QPoint(0, button->height()));
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
    setWindowTitle(QStringLiteral("BIT 充电桩应用管理平台 · 管理端"));
    resize(1380, 860);
    setMinimumSize(1080, 700);
    rootStack_ = new QStackedWidget(this);
    rootStack_->addWidget(buildLoginPage());
    rootStack_->addWidget(buildApplicationPage());
    setCentralWidget(rootStack_);

    setStyleSheet(QStringLiteral(R"(
        QMainWindow, QStackedWidget { background: #f3f6fb; }
        QWidget { color: #243044; font-family: "Noto Sans CJK SC", "Microsoft YaHei", sans-serif; font-size: 14px; }
        QFrame#panel { background: white; border: 1px solid #e7ebf1; border-radius: 9px; }
        QFrame#brandPanel { background: #1746a2; border: none; border-radius: 12px; }
        /* QFrame paints the card and its rounded corners through its normal
           style path; only LoginPage paints the artwork behind it. */
        QFrame#loginCard { background: #edf5fb; border: 1px solid #d7e5f0; border-radius: 18px; }
        QWidget#loginSurface { background: #edf5fb; }
        QFrame#loginCard QLabel { background: #edf5fb; }
        QLabel[role="loginMark"] { color: #1b5d91; font-size: 19px; font-weight: 700; letter-spacing: 1px; }
        QLabel[role="loginBrandTitle"] { color: #173653; font-size: 25px; font-weight: 600; }
        QLabel[role="loginBrandCaption"] { color: #5b7188; font-size: 14px; }
        QLabel[role="loginLabel"] { color: #31506b; font-size: 15px; font-weight: 600; }
        QLineEdit#loginInput { background: #ffffff; border: 1px solid #bfd1e3; border-radius: 9px; padding: 10px 13px; min-height: 25px; font-size: 16px; color: #173653; }
        QLineEdit#loginInput:hover { border-color: #8fb1d0; background: #ffffff; }
        QLineEdit#loginInput:focus { border: 2px solid #2f6fed; padding: 9px 12px; background: #ffffff; }
        /* Reserve this slot permanently. Empty uses the card background;
           only the hasError state receives a red surface, so failure never
           changes the positions of the button or the fields. */
        QLabel#loginError { color: transparent; background: #edf5fb; border: none; border-radius: 7px; padding: 7px 10px; font-size: 13px; }
        QLabel#loginError[hasError="true"] { color: #b42318; background: #fff3f1; border: 1px solid #f5c2bd; }
        QPushButton#loginSubmit { color: white; background: #2f6fed; border: none; border-radius: 9px; padding: 10px 16px; min-height: 27px; font-size: 16px; font-weight: 600; }
        QPushButton#loginSubmit:hover { background: #2459c5; }
        QPushButton#loginSubmit:pressed { background: #19449e; }
        QLabel[role="hero"] { color: white; font-size: 27px; font-weight: 600; }
        QLabel[role="heroSub"] { color: #cdddff; font-size: 14px; }
        QLabel[role="title"] { font-size: 23px; font-weight: 600; }
        QLabel[role="sectionTitle"] { font-size: 17px; font-weight: 600; }
        QLabel[role="muted"] { color: #748096; }
        QLabel[role="metric"] { font-size: 25px; font-weight: 600; color: #172033; }
        QLabel[role="error"] { color: #c33838; }
        QLabel[role="badgeOk"] { color: #157347; background: #e7f7ed; padding: 6px 10px; border-radius: 12px; }
        QLabel[role="badgeBad"] { color: #a61b1b; background: #ffe9e9; padding: 6px 10px; border-radius: 12px; }
        QLineEdit, QComboBox, QSpinBox, QDoubleSpinBox, QDateEdit { background: white; border: 1px solid #d8dfeb; border-radius: 6px; padding: 6px 9px; min-height: 20px; }
        QLineEdit:focus, QComboBox:focus, QSpinBox:focus, QDoubleSpinBox:focus { border: 1px solid #2f6fed; }
        QPushButton { background: #eef3f9; border: none; border-radius: 6px; padding: 7px 13px; color: #244168; font-weight: 500; }
        QPushButton:hover { background: #dfe8f7; }
        QPushButton[primary="true"] { color: white; background: #2f6fed; }
        QPushButton[primary="true"]:hover { background: #2459c5; }
        QPushButton[danger="true"] { color: #a61b1b; background: #ffe9e9; }
        QToolButton { color: #244168; background: transparent; border: none; border-radius: 6px; padding: 6px; }
        QToolButton:hover:enabled { background: #dfe8f7; }
        QToolButton:disabled { color: #aeb8c7; }
        QFrame#navigationControls { background: #eef3f9; border: 1px solid #dfe5ef; border-radius: 8px; }
        QFrame#navigationControls QToolButton:hover:enabled { background: #dfe8f7; }
        QListWidget#navigation { background: #102a56; border: none; color: #cbd9ee; outline: none; padding: 6px; font-size: 18px; }
        QListWidget#navigation::item { border-radius: 8px; padding: 17px 14px; margin: 4px 5px; }
        QListWidget#navigation::item:selected { background: #2f6fed; color: white; }
        QListWidget#navigation::item:hover:!selected { background: #193a70; }
        QTableWidget, QTreeWidget { background: white; border: 1px solid #dfe5ef; border-radius: 8px; gridline-color: #e7ebf2; selection-background-color: #e7f0ff; selection-color: #172033; alternate-background-color: #f8faff; font-size: 16px; }
        QHeaderView::section { background: #f2f5fa; color: #46546b; border: none; border-right: 1px solid #e3e8f0; border-bottom: 1px solid #d9e1ec; padding: 13px 12px; font-size: 16px; font-weight: 600; }
        QWidget#managementPage QLineEdit,
        QWidget#managementPage QComboBox,
        QWidget#managementPage QDateEdit,
        QWidget#managementPage QSpinBox,
        QWidget#managementPage QDoubleSpinBox {
            font-size: 15px;
            min-height: 32px;
            padding: 6px 11px;
        }
        QWidget#managementPage QComboBox::drop-down { width: 28px; }
        QWidget#managementPage QPushButton {
            font-size: 15px;
            min-height: 34px;
            padding: 7px 15px;
        }
        QLabel[role="pageTitle"] { font-size: 30px; font-weight: 600; }
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
    auto *page = new LoginPage(this);
    page->setObjectName(QStringLiteral("loginPage"));
    auto *outer = new QVBoxLayout(page);
    outer->setContentsMargins(56, 38, 56, 38);
    outer->setSpacing(0);
    auto *card = new QFrame(page);
    card->setObjectName(QStringLiteral("loginCard"));
    card->setFixedSize(500, 548);
    auto *cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(38, 30, 38, 32);
    cardLayout->setSpacing(0);

    // All form rows share the same opaque surface inside the card.
    auto *surface = new QWidget(card);
    surface->setObjectName(QStringLiteral("loginSurface"));
    surface->setAutoFillBackground(true);
    QPalette surfacePalette = surface->palette();
    surfacePalette.setColor(QPalette::Window, QColor(QStringLiteral("#edf5fb")));
    surface->setPalette(surfacePalette);
    auto *formLayout = new QVBoxLayout(surface);
    formLayout->setContentsMargins(0, 0, 0, 0);
    formLayout->setSpacing(0);
    cardLayout->addWidget(surface, 1);

    auto *mark = new QLabel(QStringLiteral("⚡  BIT CHARGE"), surface);
    mark->setAutoFillBackground(true);
    mark->setProperty("role", "loginMark");
    mark->setAlignment(Qt::AlignCenter);
    formLayout->addWidget(mark);
    auto *brandTitle = new QLabel(QStringLiteral("充电桩应用管理平台"), surface);
    brandTitle->setAutoFillBackground(true);
    brandTitle->setProperty("role", "loginBrandTitle");
    brandTitle->setWordWrap(false);
    brandTitle->setMinimumHeight(34);
    brandTitle->setAlignment(Qt::AlignCenter);
    formLayout->addWidget(brandTitle);
    formLayout->addSpacing(22);

    formLayout->setSpacing(8);
    auto *loginTitle = heading(QStringLiteral("管理员登录"), surface, "title");
    loginTitle->setAutoFillBackground(true);
    loginTitle->setStyleSheet(QStringLiteral("font-size:24px;font-weight:600;color:#172033;"));
    loginTitle->setAlignment(Qt::AlignCenter);
    formLayout->addWidget(loginTitle);
    formLayout->addSpacing(14);
    auto *usernameLabel = new QLabel(QStringLiteral("账号"), surface);
    usernameLabel->setAutoFillBackground(true);
    usernameLabel->setProperty("role", "loginLabel");
    formLayout->addWidget(usernameLabel);
    usernameEdit_ = new QLineEdit(surface);
    usernameEdit_->setObjectName(QStringLiteral("loginInput"));
    usernameEdit_->setPlaceholderText(QStringLiteral("请输入管理员账号"));
    formLayout->addWidget(usernameEdit_);
    auto *passwordLabel = new QLabel(QStringLiteral("密码"), surface);
    passwordLabel->setAutoFillBackground(true);
    passwordLabel->setProperty("role", "loginLabel");
    formLayout->addWidget(passwordLabel);
    passwordEdit_ = new QLineEdit(surface);
    passwordEdit_->setObjectName(QStringLiteral("loginInput"));
    passwordEdit_->setEchoMode(QLineEdit::Password);
    passwordEdit_->setPlaceholderText(QStringLiteral("请输入密码"));
    formLayout->addWidget(passwordEdit_);
    loginError_ = new QLabel(surface);
    loginError_->setAutoFillBackground(true);
    loginError_->setObjectName(QStringLiteral("loginError"));
    loginError_->setProperty("role", "error");
    loginError_->setFixedHeight(34);
    loginError_->setWordWrap(true);
    // Keep a fixed, blank slot below the password.  This prevents the
    // button/card geometry from jumping when an error is shown, while the
    // stylesheet gives the empty state the same background as the card.
    loginError_->setVisible(true);
    loginError_->setProperty("hasError", false);
    formLayout->addWidget(loginError_);
    auto *loginButton = new QPushButton(QStringLiteral("登录管理后台"), surface);
    loginButton->setObjectName(QStringLiteral("loginSubmit"));
    connect(loginButton, &QPushButton::clicked, this, &AdminWindow::attemptLogin);
    connect(passwordEdit_, &QLineEdit::returnPressed, this, &AdminWindow::attemptLogin);
    formLayout->addWidget(loginButton);
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
    sidebar->setStyleSheet(QStringLiteral("background:#102a56;"));
    auto *sidebarLayout = new QVBoxLayout(sidebar);
    sidebarLayout->setContentsMargins(12, 24, 12, 18);
    auto *logo = new QLabel(QStringLiteral("⚡ 充电桩管理平台"), sidebar);
    logo->setStyleSheet(QStringLiteral("color:white;font-size:24px;font-weight:700;padding:4px 2px;"));
    logo->setMinimumHeight(40);
    sidebarLayout->addWidget(logo);
    navigation_ = new QListWidget(sidebar);
    navigation_->setObjectName(QStringLiteral("navigation"));
    navigation_->addItems({QStringLiteral("◉  运营监控"), QStringLiteral("▥  营收统计"),
                           QStringLiteral("⌂  充电站管理"), QStringLiteral("ϟ  充电桩管理"),
                           QStringLiteral("♙  用户管理"), QStringLiteral("≡  订单管理")});
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
    accountLayout->setSpacing(0);
    auto *logoutButton = new QPushButton(QStringLiteral("退出登录"), accountPanel);
    connect(logoutButton, &QPushButton::clicked, this, [this] {
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
    backButton_->setIcon(style()->standardIcon(QStyle::SP_ArrowBack));
    backButton_->setIconSize(QSize(20, 20));
    backButton_->setToolTip(QStringLiteral("后退"));
    backButton_->setAccessibleName(QStringLiteral("后退"));
    backButton_->setAutoRaise(true);
    backButton_->setFixedSize(36, 36);
    refreshButton_ = new QToolButton(mainArea);
    refreshButton_->setIcon(style()->standardIcon(QStyle::SP_BrowserReload));
    refreshButton_->setIconSize(QSize(20, 20));
    refreshButton_->setToolTip(QStringLiteral("刷新当前页面"));
    refreshButton_->setAccessibleName(QStringLiteral("刷新当前页面"));
    refreshButton_->setAutoRaise(true);
    refreshButton_->setFixedSize(36, 36);
    forwardButton_ = new QToolButton(mainArea);
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
    rangeRow->addStretch();
    dashboardDays_ = new QComboBox(page);
    dashboardDays_->addItem(QStringLiteral("近 7 日"), 7);
    dashboardDays_->addItem(QStringLiteral("近 30 日"), 30);
    dashboardDays_->addItem(QStringLiteral("自定义"), -1);
    dashboardStartDate_ = new QDateEdit(QDate::currentDate().addDays(-6), page);
    dashboardStartDate_->setCalendarPopup(true);
    dashboardStartDate_->setDisplayFormat(QStringLiteral("yyyy-MM-dd"));
    dashboardEndDate_ = new QDateEdit(QDate::currentDate(), page);
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
                dashboardStartLabel_->setVisible(custom);
                dashboardStartDate_->setVisible(custom);
                dashboardEndLabel_->setVisible(custom);
                dashboardEndDate_->setVisible(custom);
                dashboardApplyButton_->setVisible(custom);
                if (!custom) refreshDashboard();
            });
    connect(dashboardApplyButton_, &QPushButton::clicked,
            this, &AdminWindow::refreshDashboard);
    rangeRow->addWidget(dashboardDays_);
    rangeRow->addWidget(dashboardStartLabel_);
    rangeRow->addWidget(dashboardStartDate_);
    rangeRow->addWidget(dashboardEndLabel_);
    rangeRow->addWidget(dashboardEndDate_);
    rangeRow->addWidget(dashboardApplyButton_);
    layout->addLayout(rangeRow);
    auto *metrics = new QGridLayout;
    metrics->setSpacing(14);
    metrics->addWidget(metricCard(QStringLiteral("今日营收"), QStringLiteral("#2f6fed"), &todayRevenue_, page), 0, 0);
    metrics->addWidget(metricCard(QStringLiteral("本月营收"), QStringLiteral("#13a06f"), &monthRevenue_, page), 0, 1);
    metrics->addWidget(metricCard(QStringLiteral("累计营收"), QStringLiteral("#f29d38"), &totalRevenue_, page), 0, 2);
    metrics->addWidget(metricCard(QStringLiteral("站点 / 电桩"), QStringLiteral("#875bd8"), &resourceCount_, page), 0, 3);
    layout->addLayout(metrics);
    auto *chartPanel = panel(page);
    auto *chartLayout = new QVBoxLayout(chartPanel);
    chartLayout->setContentsMargins(20, 18, 20, 16);
    chartLayout->addWidget(heading(QStringLiteral("营收趋势（元）"), chartPanel));
    revenueChart_ = new RevenueChart(chartPanel);
    chartLayout->addWidget(revenueChart_, 1);
    layout->addWidget(chartPanel, 1);
    return page;
}

QWidget *AdminWindow::buildOperationsPage()
{
    auto *page = new QWidget(this);
    page->setObjectName(QStringLiteral("managementPage"));
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(14);

    auto *overview = new QGridLayout;
    overview->setHorizontalSpacing(14);
    overview->setVerticalSpacing(14);
    auto *chartPanel = panel(page);
    auto *chartLayout = new QVBoxLayout(chartPanel);
    chartLayout->setContentsMargins(20, 18, 20, 16);
    chartLayout->addWidget(heading(QStringLiteral("电桩状态分布"), chartPanel));
    pileStatusChart_ = new PileStatusChart(chartPanel);
    chartLayout->addWidget(pileStatusChart_, 1);
    overview->addWidget(chartPanel, 0, 0);

    auto *statsPanel = panel(page);
    auto *statsLayout = new QVBoxLayout(statsPanel);
    statsLayout->setContentsMargins(20, 18, 20, 16);
    statsLayout->addWidget(heading(QStringLiteral("站点电桩统计"), statsPanel));
    operationsTable_ = new QTableWidget(statsPanel);
    prepareTable(operationsTable_, {QStringLiteral("站点"), QStringLiteral("总数"),
                                    QStringLiteral("空闲"), QStringLiteral("在用"),
                                    QStringLiteral("离线"), QStringLiteral("故障")});
    operationsTable_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    operationsTable_->verticalHeader()->setDefaultSectionSize(44);
    operationsTable_->setSelectionMode(QAbstractItemView::NoSelection);
    statsLayout->addWidget(operationsTable_, 1);
    // The station table occupies the complete width below the chart. The
    // upper-right cell intentionally remains empty for a future module.
    overview->addWidget(statsPanel, 1, 0, 1, 2);
    // The right side is intentionally left open for a future operations
    // module. Keep the current monitoring content anchored to the upper-left
    // without introducing an empty placeholder panel.
    overview->setColumnStretch(0, 3);
    overview->setColumnStretch(1, 2);
    overview->setRowStretch(0, 3);
    overview->setRowStretch(1, 2);
    layout->addLayout(overview, 1);

    connect(pileStatusChart_, &PileStatusChart::statusClicked, this,
            &AdminWindow::navigateToPileStatus);
    return page;
}

QWidget *AdminWindow::buildStationsPage()
{
    auto *page = new QWidget(this);
    page->setObjectName(QStringLiteral("managementPage"));
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    auto *controls = new QHBoxLayout;
    controls->setSpacing(10);
    stationSearchField_ = new QComboBox(page);
    stationSearchField_->addItems({QStringLiteral("站点名称"), QStringLiteral("地址"), QStringLiteral("全部")});
    stationSearchField_->setCurrentIndex(2);
    stationSearch_ = new QLineEdit(page);
    stationSearch_->setPlaceholderText(QStringLiteral("搜索"));
    stationSearch_->setMaximumWidth(300);
    stationRegion_ = new QComboBox(page);
    stationRegion_->addItem(QStringLiteral("全部区域"), QString{});
    for (const QString &region : {QStringLiteral("浑南区"), QStringLiteral("和平区"),
                                  QStringLiteral("沈北新区"), QStringLiteral("沈河区"),
                                  QStringLiteral("铁西区")}) {
        stationRegion_->addItem(region, region);
    }
    stationStatus_ = new QComboBox(page);
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
                                     QStringLiteral("可用 / 总数"), QStringLiteral("在线率"), QStringLiteral("电价"), QStringLiteral("状态")});
    stationsTable_->setRootIsDecorated(true);
    // 展开/收起只允许通过左侧树形小三角，双击行仅用于打开详情。
    stationsTable_->setExpandsOnDoubleClick(false);
    stationsTable_->setAlternatingRowColors(true);
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
    pileSearchField_ = new QComboBox(page);
    pileSearchField_->addItems({QStringLiteral("电桩编号"), QStringLiteral("所属站点"), QStringLiteral("全部")});
    pileSearchField_->setCurrentIndex(2);
    controls->addWidget(pileSearchField_);
    pileSearch_ = new QLineEdit(page);
    pileSearch_->setPlaceholderText(QStringLiteral("搜索"));
    pileSearch_->setMaximumWidth(240);
    pileStation_ = new QComboBox(page);
    pileStation_->addItem(QStringLiteral("全部站点"), QVariant{});
    pileStatus_ = new QComboBox(page);
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
        selectedPileStations_.clear(); selectedPileStatuses_.clear(); updateFilterButton(pileStationFilter_, 0); updateFilterButton(pileStatusFilter_, 0); refreshPiles();
    });
    controls->addWidget(pileSearch_);
    auto *createButton = new QPushButton(QStringLiteral("＋ 新增电桩"), page);
    createButton->setProperty("primary", true);
    connect(createButton, &QPushButton::clicked, this,
            [this] { showCreatePileDialog(); });
    pilesTable_ = new QTableWidget(page);
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
    userSearchField_ = new QComboBox(page);
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
    userStatus_ = new QComboBox(page);
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
    orderSearchField_ = new QComboBox(page);
    orderSearchField_->addItems({QStringLiteral("订单号"), QStringLiteral("用户手机号"), QStringLiteral("电桩编号"), QStringLiteral("全部")});
    orderSearchField_->setCurrentIndex(3);
    controls->addWidget(orderSearchField_);
    orderSearch_ = new QLineEdit(page);
    orderSearch_->setPlaceholderText(QStringLiteral("搜索"));
    orderSearch_->setMaximumWidth(260);
    auto *resetButton = new QPushButton(QStringLiteral("重置"), page);
    orderStatus_ = new QComboBox(page);
    orderStatus_->addItem(QStringLiteral("全部状态"), QString{});
    for (const QString &status : {QStringLiteral("RESERVED"), QStringLiteral("CHARGING"), QStringLiteral("PENDING_PAYMENT"), QStringLiteral("COMPLETED"), QStringLiteral("CANCELLED")}) {
        orderStatus_->addItem(orderStatusText(status), status);
    }
    orderMode_ = new QComboBox(page);
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
        selectedOrderStatuses_.clear(); selectedOrderModes_.clear(); updateFilterButton(orderStatusFilter_, 0); updateFilterButton(orderModeFilter_, 0); refreshOrders();
    });
    controls->addWidget(orderSearch_);
    orderStatusFilter_ = new QPushButton(page); orderStatusFilter_->setProperty("filterTitle", QStringLiteral("状态")); updateFilterButton(orderStatusFilter_, 0);
    orderModeFilter_ = new QPushButton(page); orderModeFilter_->setProperty("filterTitle", QStringLiteral("模式")); updateFilterButton(orderModeFilter_, 0);
    installMultiSelectMenu(orderStatusFilter_, orderStatus_, [this](const QVariant &v){ return selectedOrderStatuses_.contains(v.toString()); }, [this](const QVariant &v,bool on){ if(on) selectedOrderStatuses_.insert(v.toString()); else selectedOrderStatuses_.remove(v.toString()); updateFilterButton(orderStatusFilter_, selectedOrderStatuses_.size()); }, [this]{ selectedOrderStatuses_.clear(); updateFilterButton(orderStatusFilter_, 0); }, [this]{ refreshOrders(); });
    installMultiSelectMenu(orderModeFilter_, orderMode_, [this](const QVariant &v){ return selectedOrderModes_.contains(v.toString()); }, [this](const QVariant &v,bool on){ if(on) selectedOrderModes_.insert(v.toString()); else selectedOrderModes_.remove(v.toString()); updateFilterButton(orderModeFilter_, selectedOrderModes_.size()); }, [this]{ selectedOrderModes_.clear(); updateFilterButton(orderModeFilter_, 0); }, [this]{ refreshOrders(); });
    controls->addWidget(orderStatusFilter_);
    controls->addWidget(orderModeFilter_);
    controls->addWidget(resetButton);
    controls->addStretch();
    layout->addLayout(controls);
    ordersTable_ = new QTableWidget(page);
    prepareTable(ordersTable_, {QStringLiteral("订单 ID"), QStringLiteral("订单号"), QStringLiteral("用户手机号"), QStringLiteral("站点"), QStringLiteral("电桩"), QStringLiteral("模式"), QStringLiteral("状态"), QStringLiteral("金额")});
    ordersTable_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    ordersTable_->horizontalHeader()->setSectionResizeMode(7, QHeaderView::ResizeToContents);
    ordersTable_->verticalHeader()->setDefaultSectionSize(48);
    connect(ordersTable_, &QTableWidget::cellDoubleClicked, this, [this](int row,int){ if(row>=0) showOrderDetails(ordersTable_->item(row,0)->data(Qt::UserRole).toLongLong()); });
    ordersTable_->setContextMenuPolicy(Qt::CustomContextMenu);
    orderStatus_->hide(); orderMode_->hide();
    connect(ordersTable_, &QTableWidget::customContextMenuRequested, this, [this](const QPoint &pos){ const int row=ordersTable_->rowAt(pos.y()); if(row<0)return; ordersTable_->selectRow(row); QMenu menu(this); menu.addAction(QStringLiteral("查看详情"),this,[this,row]{showOrderDetails(ordersTable_->item(row,0)->data(Qt::UserRole).toLongLong());}); menu.addAction(QStringLiteral("复制订单号"),this,[this,row]{QApplication::clipboard()->setText(ordersTable_->item(row,1)->text());}); menu.exec(ordersTable_->viewport()->mapToGlobal(pos)); });
    layout->addWidget(ordersTable_, 1);
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
    if (facade_ == nullptr) {
        setLoginError(QStringLiteral("管理服务尚未初始化"));
        return;
    }
    const ServiceResult result = facade_->login(usernameEdit_->text().trimmed(), passwordEdit_->text());
    if (!result.ok()) {
        setLoginError(QStringLiteral("账号或密码错误，请重试"));
        passwordEdit_->selectAll();
        passwordEdit_->setFocus();
        return;
    }
    setLoginError(QString());
    rootStack_->setCurrentIndex(1);
    backHistory_.clear();
    forwardHistory_.clear();
    navigation_->setCurrentRow(0);
    updateNavigationButtons();
    refreshAll();
}

void AdminWindow::selectPage(int index)
{
    if (index < 0 || contentStack_ == nullptr) return;
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
                                    QStringLiteral("用户管理"), QStringLiteral("订单管理")};
    contentStack_->setCurrentIndex(index);
    {
        const QSignalBlocker navigationBlocker(navigation_);
        navigation_->setCurrentRow(index);
    }
    pageTitle_->setText(titles.value(index));
    switch (index) {
    case 0: refreshOperations(); break;
    case 1: refreshDashboard(); break;
    case 2: refreshStations(); break;
    case 3: refreshPiles(); break;
    case 4: refreshUsers(); break;
    case 5: refreshOrders(); break;
    default: break;
    }
    updateNavigationButtons();
}

void AdminWindow::refreshAll()
{
    refreshDashboard();
    refreshOperations();
    refreshStations();
    refreshPiles();
    refreshUsers();
    refreshOrders();
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
        if (dashboardStartDate_ != nullptr) dashboardStartDate_->setDate(QDate::currentDate().addDays(-6));
        if (dashboardEndDate_ != nullptr) dashboardEndDate_->setDate(QDate::currentDate());
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
        refreshOrders();
        break;
    }
    default:
        break;
    }
    updateNavigationButtons();
}

void AdminWindow::refreshDashboard()
{
    if (facade_ == nullptr || dashboardDays_ == nullptr) return;
    const int days = dashboardDays_->currentData().toInt();
    const ServiceResult result = days < 0
        ? facade_->getDashboard(dashboardStartDate_->date(), dashboardEndDate_->date())
        : facade_->getDashboard(days);
    if (!result.ok()) return showServiceError(result.code, result.message);
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
    revenueChart_->setPoints(std::move(points));
}

void AdminWindow::refreshOperations()
{
    if (facade_ == nullptr || pileStatusChart_ == nullptr || operationsTable_ == nullptr) return;
    const ServiceResult stationResult = facade_->listStations({}, {});
    const ServiceResult pileResult = facade_->listPiles();
    if (!stationResult.ok()) return showServiceError(stationResult.code, stationResult.message);
    if (!pileResult.ok()) return showServiceError(pileResult.code, pileResult.message);

    struct Counters { qint64 total = 0; qint64 idle = 0; qint64 inUse = 0; qint64 offline = 0; qint64 fault = 0; };
    QHash<qint64, QString> stationNames;
    QHash<qint64, Counters> byStation;
    qint64 idle = 0;
    qint64 inUse = 0;
    qint64 offline = 0;
    qint64 fault = 0;
    for (const QJsonValue &value : stationResult.data.value(QStringLiteral("items")).toArray()) {
        const QJsonObject station = value.toObject();
        const qint64 stationId = station.value(QStringLiteral("stationId")).toInteger();
        stationNames.insert(stationId, station.value(QStringLiteral("name")).toString());
        byStation.insert(stationId, Counters{});
    }
    for (const QJsonValue &value : pileResult.data.value(QStringLiteral("items")).toArray()) {
        const QJsonObject pile = value.toObject();
        const QString status = pile.value(QStringLiteral("status")).toString();
        Counters &counters = byStation[pile.value(QStringLiteral("stationId")).toInteger()];
        ++counters.total;
        if (status == QStringLiteral("IDLE")) { ++idle; ++counters.idle; }
        else if (status == QStringLiteral("RESERVED") || status == QStringLiteral("CHARGING")) { ++inUse; ++counters.inUse; }
        else if (status == QStringLiteral("OFFLINE")) { ++offline; ++counters.offline; }
        else { ++fault; ++counters.fault; }
    }

    pileStatusChart_->setSlices({
        {QStringLiteral("IDLE"), QStringLiteral("空闲"), idle, QColor(QStringLiteral("#22a06b"))},
        {QStringLiteral("IN_USE"), QStringLiteral("在用"), inUse, QColor(QStringLiteral("#2f6fed"))},
        {QStringLiteral("OFFLINE"), QStringLiteral("离线"), offline, QColor(QStringLiteral("#e58b25"))},
        {QStringLiteral("FAULT"), QStringLiteral("故障"), fault, QColor(QStringLiteral("#d64545"))},
    });

    operationsTable_->setRowCount(0);
    for (auto it = stationNames.cbegin(); it != stationNames.cend(); ++it) {
        const Counters counters = byStation.value(it.key());
        const int row = operationsTable_->rowCount();
        operationsTable_->insertRow(row);
        operationsTable_->setItem(row, 0, item(it.value()));
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
    QSet<qint64> expandedIds;
    if (restoreExpandedStationsPending_) {
        expandedIds = pendingExpandedStations_;
        restoreExpandedStationsPending_ = false;
        pendingExpandedStations_.clear();
    }
    for (int index = 0; index < stationsTable_->topLevelItemCount(); ++index) {
        auto *item = stationsTable_->topLevelItem(index);
        if (item->isExpanded()) expandedIds.insert(item->data(0, Qt::UserRole).toLongLong());
    }
    const QString region;
    const ServiceResult result = facade_->listStations(region, {});
    if (!result.ok()) return showServiceError(result.code, result.message);
    const QJsonArray rows = result.data.value(QStringLiteral("items")).toArray();
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
        stationItem->setForeground(6, status == QStringLiteral("ACTIVE") ? QColor(QStringLiteral("#15803d")) : QColor(QStringLiteral("#667085")));
        QFont stationFont = stationItem->font(0);
        stationFont = stationsTable_->font();
        stationFont.setPixelSize(16);
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
                childFont.setPixelSize(14);
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
    QHash<qint64, QString> stationNames;
    if (pileStation_ != nullptr) {
        const QVariant current = pileStation_->currentData();
        const ServiceResult stations = facade_->listStations({}, {});
        QSignalBlocker blocker(pileStation_);
        pileStation_->clear();
        pileStation_->addItem(QStringLiteral("全部站点"), QVariant{});
        for (const QJsonValue &value : stations.data.value(QStringLiteral("items")).toArray()) {
            const QJsonObject station = value.toObject();
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
    pileSearch_->clear();
    appliedPileSearch_.clear();
    // 清除当前可能隐藏目标电桩的多选筛选，并将目标站点作为唯一站点筛选。
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
        auto *statusItem = item(userStatusText(status));
        statusItem->setData(Qt::UserRole, status);
        colorStatus(statusItem, status);
        usersTable_->setItem(row, 4, statusItem);
    }
}

void AdminWindow::refreshOrders()
{
    if (facade_ == nullptr || ordersTable_ == nullptr) return;
    const ServiceResult result = facade_->listOrders();
    if (!result.ok()) return showServiceError(result.code, result.message);
    const QJsonArray rows = result.data.value(QStringLiteral("items")).toArray();
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
    for (const QJsonValue &value : rows) {
        const QJsonObject order = value.toObject();
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
        ordersTable_->setItem(row, 5, item(order.value(QStringLiteral("mode")).toString() == QStringLiteral("DIRECT") ? QStringLiteral("直接充电") : QStringLiteral("预约")));
        auto *statusItem = item(orderStatusText(status));
        colorStatus(statusItem, status);
        ordersTable_->setItem(row, 6, statusItem);
        ordersTable_->setItem(row, 7, item(moneyText(order.value(QStringLiteral("amountCents")).toInteger())));
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
    auto *region = new QComboBox(&dialog);
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
    layout->addRow(QStringLiteral("单价（分/kWh）"), price);
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
        auto *type = new QComboBox(pileTable);
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
    auto *region = new QComboBox(&dialog);
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
    layout->addRow(QStringLiteral("单价（分/kWh）"), price);

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
    if (stationId <= 0 || pileSearch_ == nullptr || pileSearchField_ == nullptr) return;
    if (historyReady_ && !restoringHistory_) {
        pushNavigationHistory();
        skipNextNavigationHistory_ = navigation_->currentRow() != 3;
    }
    pileSearch_->clear();
    appliedPileSearch_.clear();
    pileSearchField_->setCurrentIndex(2);
    selectedPileStatuses_.clear();
    selectedPileStations_.clear();
    selectedPileStations_.insert(stationId);
    updateFilterButton(pileStationFilter_, 1);
    updateFilterButton(pileStatusFilter_, 0);
    if (navigation_->currentRow() == 3) {
        refreshPiles();
    } else {
        navigation_->setCurrentRow(3);
    }
}

void AdminWindow::navigateToPileStatus(const QString &statusKey)
{
    if (pileSearch_ == nullptr || pileSearchField_ == nullptr
        || pileStatusFilter_ == nullptr) return;
    if (historyReady_ && !restoringHistory_) {
        pushNavigationHistory();
        skipNextNavigationHistory_ = navigation_->currentRow() != 3;
    }
    pileSearch_->clear();
    appliedPileSearch_.clear();
    pileSearchField_->setCurrentIndex(2);
    selectedPileStations_.clear();
    selectedPileStatuses_.clear();
    if (statusKey == QStringLiteral("IDLE")) {
        selectedPileStatuses_.insert(QStringLiteral("IDLE"));
    } else if (statusKey == QStringLiteral("IN_USE")) {
        selectedPileStatuses_.insert(QStringLiteral("RESERVED"));
        selectedPileStatuses_.insert(QStringLiteral("CHARGING"));
    } else if (statusKey == QStringLiteral("OFFLINE")) {
        selectedPileStatuses_.insert(QStringLiteral("OFFLINE"));
    } else if (statusKey == QStringLiteral("FAULT")) {
        selectedPileStatuses_.insert(QStringLiteral("FAULT"));
    } else {
        return;
    }
    updateFilterButton(pileStationFilter_, 0);
    updateFilterButton(pileStatusFilter_, selectedPileStatuses_.size());
    if (navigation_->currentRow() == 3) {
        refreshPiles();
    } else {
        navigation_->setCurrentRow(3);
    }
}

AdminWindow::PageState AdminWindow::capturePageState() const
{
    PageState state;
    state.pageIndex = contentStack_ == nullptr ? 0 : contentStack_->currentIndex();
    if (stationSearch_ != nullptr) state.stationSearch = stationSearch_->text();
    if (stationSearchField_ != nullptr) state.stationSearchField = stationSearchField_->currentIndex();
    state.stationRegions = selectedStationRegions_;
    state.stationStatuses = selectedStationStatuses_;
    if (stationsTable_ != nullptr) {
        for (int index = 0; index < stationsTable_->topLevelItemCount(); ++index) {
            const auto *station = stationsTable_->topLevelItem(index);
            if (station->isExpanded()) state.expandedStations.insert(station->data(0, Qt::UserRole).toLongLong());
        }
        if (stationsTable_->currentItem() != nullptr) {
            const auto *current = stationsTable_->currentItem();
            state.selectedStationId = current->parent() == nullptr
                ? current->data(0, Qt::UserRole).toLongLong()
                : current->data(0, Qt::UserRole).toLongLong();
        }
    }
    if (pileSearch_ != nullptr) state.pileSearch = pileSearch_->text();
    if (pileSearchField_ != nullptr) state.pileSearchField = pileSearchField_->currentIndex();
    state.pileStations = selectedPileStations_;
    state.pileStatuses = selectedPileStatuses_;
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
    if (ordersTable_ != nullptr && ordersTable_->currentRow() >= 0) {
        state.selectedOrderId = ordersTable_->item(ordersTable_->currentRow(), 0)->data(Qt::UserRole).toLongLong();
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
    restoringHistory_ = true;
    if (stationSearch_ != nullptr) stationSearch_->setText(state.stationSearch);
    if (stationSearchField_ != nullptr) stationSearchField_->setCurrentIndex(state.stationSearchField);
    selectedStationRegions_ = state.stationRegions;
    selectedStationStatuses_ = state.stationStatuses;
    if (pileSearch_ != nullptr) pileSearch_->setText(state.pileSearch);
    if (pileSearchField_ != nullptr) pileSearchField_->setCurrentIndex(state.pileSearchField);
    selectedPileStations_ = state.pileStations;
    selectedPileStatuses_ = state.pileStatuses;
    if (userSearch_ != nullptr) userSearch_->setText(state.userSearch);
    if (userSearchField_ != nullptr) userSearchField_->setCurrentIndex(state.userSearchField);
    selectedUserStatuses_ = state.userStatuses;
    if (orderSearch_ != nullptr) orderSearch_->setText(state.orderSearch);
    if (orderSearchField_ != nullptr) orderSearchField_->setCurrentIndex(state.orderSearchField);
    selectedOrderStatuses_ = state.orderStatuses;
    selectedOrderModes_ = state.orderModes;
    if (dashboardDays_ != nullptr) {
        const int index = dashboardDays_->findData(state.dashboardDays);
        if (index >= 0) dashboardDays_->setCurrentIndex(index);
    }
    if (dashboardStartDate_ != nullptr && state.dashboardStartDate.isValid()) dashboardStartDate_->setDate(state.dashboardStartDate);
    if (dashboardEndDate_ != nullptr && state.dashboardEndDate.isValid()) dashboardEndDate_->setDate(state.dashboardEndDate);
    updateFilterButton(stationRegionFilter_, selectedStationRegions_.size());
    updateFilterButton(stationStatusFilter_, selectedStationStatuses_.size());
    updateFilterButton(pileStationFilter_, selectedPileStations_.size());
    updateFilterButton(pileStatusFilter_, selectedPileStatuses_.size());
    updateFilterButton(userStatusFilter_, selectedUserStatuses_.size());
    updateFilterButton(orderStatusFilter_, selectedOrderStatuses_.size());
    updateFilterButton(orderModeFilter_, selectedOrderModes_.size());

    pendingExpandedStations_ = state.expandedStations;
    restoreExpandedStationsPending_ = true;
    focusPileAfterRefresh_ = state.selectedPileId;
    if (navigation_ != nullptr) navigation_->setCurrentRow(state.pageIndex);
    else selectPage(state.pageIndex);
    if (state.pageIndex == 2 && stationsTable_ != nullptr && state.selectedStationId > 0) {
        for (int index = 0; index < stationsTable_->topLevelItemCount(); ++index) {
            auto *station = stationsTable_->topLevelItem(index);
            if (station->data(0, Qt::UserRole).toLongLong() == state.selectedStationId) {
                stationsTable_->setCurrentItem(station);
                break;
            }
        }
    } else if (state.pageIndex == 4 && usersTable_ != nullptr && state.selectedUserId > 0) {
        for (int row = 0; row < usersTable_->rowCount(); ++row) {
            if (usersTable_->item(row, 0)->data(Qt::UserRole).toLongLong() == state.selectedUserId) {
                usersTable_->selectRow(row);
                break;
            }
        }
    } else if (state.pageIndex == 5 && ordersTable_ != nullptr && state.selectedOrderId > 0) {
        for (int row = 0; row < ordersTable_->rowCount(); ++row) {
            if (ordersTable_->item(row, 0)->data(Qt::UserRole).toLongLong() == state.selectedOrderId) {
                ordersTable_->selectRow(row);
                break;
            }
        }
    }
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
    auto *station = new QComboBox(&dialog);
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
    auto *type = new QComboBox(&dialog);
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
    auto *type = new QComboBox(&dialog);
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
    QString text = QStringLiteral("站点：%1\nID：%2\n区域：%3\n地址：%4\n坐标：%5, %6\n电价：¥%7/度\n状态：%8\n可用/总数：%9/%10\n在线率：%11%\n\n所属电桩：")
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
            .arg(user.value(QStringLiteral("createdAt")).toString()).arg(userId).arg(count).arg(moneyText(spent)));
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
            return value.isString() ? value.toString() : QStringLiteral("—");
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
    Q_UNUSED(code)
    QString detail = message;
    if (code == ErrorCode::CurrentOrderExists) detail = QStringLiteral("该用户存在未结束订单，暂时不能冻结。");
    else if (message == QStringLiteral("STATION_HAS_ACTIVE_PILES")) {
        detail = QStringLiteral("该充电站有已预约或正在充电的电桩，结束相关订单后才能停用。");
    } else if (code == ErrorCode::IllegalOrderState) detail = QStringLiteral("充电中或已预约的电桩不能重启。");
    else if (code == ErrorCode::InvalidRequest) detail = QStringLiteral("输入内容不完整或格式不正确。");
    if (message == QStringLiteral("INVALID_STATION")) detail = QStringLiteral("请选择一个已启用的充电站。");
    else if (message == QStringLiteral("PILE_CODE_EXISTS")) detail = QStringLiteral("电桩编号已存在，请使用其他编号。");
    QMessageBox::warning(this, QStringLiteral("操作失败"), detail);
}

void AdminWindow::prepareTable(QTableWidget *table, const QStringList &headers)
{
    table->setColumnCount(headers.size());
    table->setHorizontalHeaderLabels(headers);
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
