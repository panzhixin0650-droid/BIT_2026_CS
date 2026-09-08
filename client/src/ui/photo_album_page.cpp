#include "ui/photo_album_page.h"
#include "ui/avatar_art.h"

#include <QDir>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QImageReader>
#include <QLabel>
#include <QListWidget>
#include <QPainter>
#include <QPushButton>
#include <QResizeEvent>
#include <QStackedWidget>
#include <QStyledItemDelegate>
#include <QStyle>
#include <QTimer>
#include <QVBoxLayout>

static void initializeAlbumResources()
{
    Q_INIT_RESOURCE(album_resources);
}

namespace charging::client {
namespace {
constexpr int pathRole = Qt::UserRole;
constexpr int imageRole = Qt::UserRole + 1;

QImage readAlbumImage(const QString &path, int maximumSide)
{
    const QFileInfo file(path);
    if (!file.isFile() || file.size() > 20 * 1024 * 1024) return {};
    QImageReader reader(path);
    reader.setAutoTransform(true);
    const QSize size = reader.size();
    if (!size.isValid() || qint64(size.width()) * size.height() > 32000000) return {};
    if (qMax(size.width(), size.height()) > maximumSide)
        reader.setScaledSize(size.scaled(maximumSide, maximumSide, Qt::KeepAspectRatio));
    return reader.read();
}

class PhotoDelegate final : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    QSize sizeHint(const QStyleOptionViewItem &, const QModelIndex &) const override
    {
        const auto *view = qobject_cast<QListWidget *>(parent());
        return view && view->gridSize().isValid() ? view->gridSize() : QSize(90, 114);
    }
    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override
    {
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        const QRect tile = option.rect.adjusted(2, 2, -2, -24);
        const bool selected = option.state.testFlag(QStyle::State_Selected);
        painter->fillRect(tile, Qt::white);
        const auto image = qvariant_cast<QPixmap>(index.data(Qt::DecorationRole));
        const QSize fitted = image.size().scaled(tile.size() - QSize(8, 8), Qt::KeepAspectRatio);
        painter->drawPixmap(QRect(tile.center() - QPoint(fitted.width()/2, fitted.height()/2), fitted), image);
        painter->setBrush(Qt::NoBrush);
        painter->setPen(QPen(QColor(selected ? "#245c45" : "#e1e7dc"), selected ? 3 : 1));
        painter->drawRect(tile.adjusted(1, 1, -1, -1));
        const QRectF circle(tile.right() - 24, tile.top() + 5, 18, 18);
        painter->setBrush(QColor(selected ? "#245c45" : "#ffffff"));
        painter->setPen(QPen(QColor(selected ? "#245c45" : "#97ac96"), 1.5));
        painter->drawEllipse(circle);
        if (selected) {
            painter->setPen(Qt::white);
            painter->drawText(circle, Qt::AlignCenter, QStringLiteral("1"));
        }
        auto font = option.font;
        font.setPixelSize(9);
        painter->setFont(font);
        painter->setPen(QColor("#65796c"));
        painter->drawText(QRect(tile.left(), tile.bottom()+3, tile.width(), 20),
                          Qt::AlignCenter, painter->fontMetrics().elidedText(
                              index.data(Qt::DisplayRole).toString(), Qt::ElideRight, tile.width()));
        painter->restore();
    }
};
} // namespace

PhotoAlbumPage::PhotoAlbumPage(QWidget *parent, const QString &directory, Purpose purpose)
    : QWidget(parent), directory_(directory), purpose_(purpose)
{
    initializeAlbumResources();
    setObjectName("photoAlbumPage");
    setStyleSheet(QStringLiteral(R"QSS(
        #photoAlbumPage { background:#f6f7f2; }
        #photoAlbumPage QLabel { color:#203d33; background:transparent; }
        #photoAlbumPage QPushButton { background:white; color:#36523f; border:1px solid #dce3d5;
                                     border-radius:12px; min-height:38px; padding:0 12px; font-size:12px; }
        #photoAlbumPage QPushButton:hover { background:#edf4e5; border-color:#92ad7e; }
        #photoAlbumPage QPushButton:disabled { color:#96a18e; background:#edf0e8; border-color:#e1e6da; }
        #photoAlbumPage #albumConfirm { background:#245c45; color:white; border-color:#245c45; font-weight:600; }
        #photoAlbumPage #albumConfirm:hover { background:#1c4d39; }
        #photoAlbumPage #albumConfirm:disabled { background:#dce4d6; color:#8d9e87; border-color:#dce4d6; }
        #photoAlbumPage QListWidget { background:#f6f7f2; border:none; padding:0; }
        #photoAlbumPage #albumPreviewImage { background:#edf1e8; }
    )QSS"));
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(8);
    auto *header = new QHBoxLayout;
    header->setContentsMargins(16, 14, 16, 0);
    back_ = new QPushButton(QStringLiteral("取消"), this);
    back_->setObjectName("albumBack");
    header->addWidget(back_);
    auto *title = new QLabel(purpose_ == Purpose::Avatar ? QStringLiteral("选择头像")
                                                        : QStringLiteral("选择照片"), this);
    title->setAlignment(Qt::AlignCenter);
    title->setStyleSheet("font-size:18px;font-weight:600;");
    header->addWidget(title, 1);
    count_ = new QLabel(this);
    count_->setStyleSheet("color:#71806e;font-size:11px;");
    header->addWidget(count_);
    root->addLayout(header);
    auto *caption = new QLabel(purpose_ == Purpose::Avatar
        ? QStringLiteral("测试相册 · 选择一张图片作为头像")
        : QStringLiteral("测试相册 · 选择一张二维码图片"), this);
    caption->setContentsMargins(16, 0, 16, 0);
    caption->setStyleSheet("color:#71806e;font-size:11px;");
    root->addWidget(caption);

    pages_ = new QStackedWidget(this);
    auto *gridPage = new QWidget(pages_);
    auto *gridLayout = new QVBoxLayout(gridPage);
    gridLayout->setContentsMargins(4, 0, 4, 0);
    photos_ = new QListWidget(gridPage);
    photos_->setObjectName("albumPhotos");
    photos_->viewport()->installEventFilter(this);
    photos_->setViewMode(QListView::IconMode);
    photos_->setMovement(QListView::Static);
    photos_->setResizeMode(QListView::Adjust);
    photos_->setSelectionMode(QAbstractItemView::SingleSelection);
    photos_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    photos_->setItemDelegate(new PhotoDelegate(photos_));
    gridLayout->addWidget(photos_, 1);
    empty_ = new QLabel(QStringLiteral("相册里还没有可读取的照片"), gridPage);
    empty_->setAlignment(Qt::AlignCenter);
    gridLayout->addWidget(empty_);
    pages_->addWidget(gridPage);
    preview_ = new QLabel(pages_);
    preview_->setObjectName("albumPreviewImage");
    preview_->setAlignment(Qt::AlignCenter);
    preview_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    pages_->addWidget(preview_);
    root->addWidget(pages_, 1);

    auto *bottom = new QHBoxLayout;
    bottom->setContentsMargins(16, 8, 16, 16);
    previewButton_ = new QPushButton(QStringLiteral("预览"), this);
    previewButton_->setObjectName("albumPreview");
    bottom->addWidget(previewButton_);
    selection_ = new QLabel(this);
    selection_->setAlignment(Qt::AlignCenter);
    selection_->setStyleSheet("color:#71806e;font-size:11px;");
    bottom->addWidget(selection_, 1);
    confirm_ = new QPushButton(QStringLiteral("识别"), this);
    confirm_->setObjectName("albumConfirm");
    confirm_->setMinimumWidth(96);
    bottom->addWidget(confirm_);
    root->addLayout(bottom);
    for (auto *button : {back_, previewButton_, confirm_}) button->setCursor(Qt::PointingHandCursor);

    connect(photos_, &QListWidget::itemSelectionChanged, this, &PhotoAlbumPage::updateSelection);
    connect(photos_, &QListWidget::itemDoubleClicked, this, [this] { showPreview(); });
    connect(previewButton_, &QPushButton::clicked, this, &PhotoAlbumPage::showPreview);
    connect(back_, &QPushButton::clicked, this, [this] {
        if (pages_->currentIndex() == 1) {
            pages_->setCurrentIndex(0); back_->setText(QStringLiteral("取消"));
            previewButton_->setEnabled(!photos_->selectedItems().isEmpty());
        } else emit backRequested();
    });
    connect(confirm_, &QPushButton::clicked, this, [this] {
        const auto items = photos_->selectedItems();
        if (items.isEmpty()) return;
        const auto image = items.first()->data(imageRole);
        if (image.isValid()) emit imageSelectedImage(qvariant_cast<QImage>(image));
        else emit imageSelected(items.first()->data(pathRole).toString());
    });
}

void PhotoAlbumPage::reload()
{
    pages_->setCurrentIndex(0);
    back_->setText(QStringLiteral("取消"));
    photos_->clear();
    QString directory = directory_;
    if (directory.isEmpty()) directory = qEnvironmentVariable("CHARGING_DEMO_ALBUM_DIR");
    if (directory.isEmpty()) {
        directory = QString::fromUtf8(CHARGING_DEMO_ALBUM_DIR);
        if (!QDir(directory).exists()) directory = QStringLiteral(":/demo-album");
    }
    const QDir folder(directory);
    if (purpose_ == Purpose::Avatar) {
        for (const auto &avatar : basicAvatars()) {
            auto *item = new QListWidgetItem(avatar.name, photos_);
            item->setData(imageRole, avatar.image);
            item->setData(Qt::DecorationRole, QPixmap::fromImage(avatar.image.scaled(
                200, 200, Qt::KeepAspectRatio, Qt::SmoothTransformation)));
        }
    }
    const auto files = folder.entryInfoList(QDir::Files | QDir::Readable, QDir::Name);
    for (const auto &file : files) {
        if (!QStringList{"png", "jpg", "jpeg", "bmp", "webp"}.contains(file.suffix().toLower())) continue;
        const auto thumbnail = readAlbumImage(file.filePath(), 200);
        if (thumbnail.isNull()) continue;
        auto *item = new QListWidgetItem(file.completeBaseName(), photos_);
        item->setData(pathRole, file.filePath());
        item->setData(Qt::DecorationRole, QPixmap::fromImage(thumbnail));
        item->setToolTip(file.fileName());
    }
    count_->setText(QStringLiteral("%1 张").arg(photos_->count()));
    empty_->setVisible(photos_->count() == 0);
    updateSelection();
    QTimer::singleShot(0, this, &PhotoAlbumPage::layoutGrid);
}

void PhotoAlbumPage::layoutGrid()
{
    const int width = photos_->viewport()->width();
    const int columns = width < 520 ? 4 : 6;
    // Reserve the scroll gutter so adding more photos does not change columns.
    const int gutter = photos_->style()->pixelMetric(QStyle::PM_ScrollBarExtent);
    const int side = qMax(44, (width - gutter - 2) / columns);
    photos_->setGridSize(QSize(side, side + 24));
    updatePreview();
}

void PhotoAlbumPage::updateSelection()
{
    const bool selected = !photos_->selectedItems().isEmpty();
    confirm_->setEnabled(selected);
    previewButton_->setEnabled(selected);
    selection_->setText(selected ? QStringLiteral("已选 1 张") : QStringLiteral("未选择"));
    confirm_->setText(purpose_ == Purpose::Avatar ? QStringLiteral("设为头像")
        : selected ? QStringLiteral("识别 (1)") : QStringLiteral("识别"));
}

void PhotoAlbumPage::showPreview()
{
    if (photos_->selectedItems().isEmpty()) return;
    pages_->setCurrentIndex(1);
    back_->setText(QStringLiteral("‹ 返回"));
    previewButton_->setEnabled(false);
    updatePreview();
}

void PhotoAlbumPage::updatePreview()
{
    if (pages_->currentIndex() != 1 || photos_->selectedItems().isEmpty()) return;
    const auto image = readAlbumImage(photos_->selectedItems().first()->data(pathRole).toString(), 1600);
    preview_->setPixmap(QPixmap::fromImage(image).scaled(preview_->size()-QSize(24,24), Qt::KeepAspectRatio, Qt::SmoothTransformation));
}

bool PhotoAlbumPage::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == photos_->viewport() && event->type() == QEvent::Resize)
        QTimer::singleShot(0, this, &PhotoAlbumPage::layoutGrid);
    return QWidget::eventFilter(watched, event);
}

void PhotoAlbumPage::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    QTimer::singleShot(0, this, &PhotoAlbumPage::layoutGrid);
}

} // namespace charging::client
