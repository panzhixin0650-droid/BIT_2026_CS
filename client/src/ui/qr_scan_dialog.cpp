#include "ui/qr_scan_dialog.h"

#include <QCamera>
#include <QFrame>
#include <QPainter>
#include <QCameraDevice>
#include <QComboBox>
#include "ui/photo_album_page.h"
#include <QStackedLayout>
#include <QHideEvent>
#include <QLabel>
#include <QMediaCaptureSession>
#include <QMediaDevices>
#include <QPushButton>
#include <QSignalBlocker>
#include <QInputDialog>
#include <QRegularExpression>
#include <QResizeEvent>
#include <QHBoxLayout>
#include <QTimer>
#include <QVBoxLayout>
#include <QVideoFrame>
#include <QVideoSink>

namespace charging::client {

namespace {
class ScanGuide final : public QWidget {
public:
    explicit ScanGuide(QWidget *parent) : QWidget(parent)
    {
        setMinimumHeight(120);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setStyleSheet("background:transparent;");
    }
protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const qreal side = qMin(230, qMin(width() - 40, height() - 24));
        const QRectF frame((width()-side)/2, (height()-side)/2, side, side);
        painter.setPen(QPen(QColor(230, 241, 217, 230), 4, Qt::SolidLine, Qt::RoundCap));
        for (const auto &point : {frame.topLeft(), frame.topRight(), frame.bottomLeft(), frame.bottomRight()}) {
            const int dx = point.x() == frame.left() ? 1 : -1;
            const int dy = point.y() == frame.top() ? 1 : -1;
            painter.drawLine(point, point + QPointF(dx * 24, 0));
            painter.drawLine(point, point + QPointF(0, dy * 24));
        }
        painter.setPen(QPen(QColor("#c4dda5"), 2, Qt::SolidLine, Qt::RoundCap));
        painter.drawLine(QPointF(frame.left()+12, frame.center().y()),
                         QPointF(frame.right()-12, frame.center().y()));
    }
};
} // namespace

QrScanDialog::QrScanDialog(Source source, QWidget *parent, bool immersive)
    : QDialog(parent), devices_(new QMediaDevices(this)),
      capture_(new QMediaCaptureSession(this)), sink_(new QVideoSink(this)),
      cameraChoice_(new QComboBox(this)), cameraButton_(new QPushButton(this)),
      imageButton_(new QPushButton(QStringLiteral("选择二维码图片"), this)),
      preview_(new QLabel(this)), status_(new QLabel(this)), worker_(new QObject),
      frameWatchdog_(new QTimer(this))
{
    immersive_ = immersive;
    setObjectName(QStringLiteral("qrScanDialog"));
    setWindowTitle(QStringLiteral("识别充电桩二维码"));
    setModal(true);
    resize(parent ? qBound(300, parent->width() - 24, 420) : 420,
           parent ? qBound(420, parent->height() - 48, 520) : 480);
    pages_ = new QStackedLayout(this);
    pages_->setContentsMargins(0,0,0,0);
    cameraPage_ = new QWidget(this);
    pages_->addWidget(cameraPage_);
    auto *layout = new QVBoxLayout(cameraPage_);
    auto *hint = new QLabel(QStringLiteral("识别成功后前往充电页，确认后开始充电。"), this);
    hint->setWordWrap(true);
    layout->addWidget(hint);
    cameraChoice_->setObjectName(QStringLiteral("qrCameraChoice"));
    cameraButton_->setObjectName(QStringLiteral("qrCameraButton"));
    imageButton_->setObjectName(QStringLiteral("qrImageButton"));
    layout->addWidget(cameraChoice_);
    layout->addWidget(cameraButton_);
    preview_->setMinimumSize(240, 160);
    preview_->setObjectName(QStringLiteral("qrCameraPreview"));
    preview_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Expanding);
    preview_->setAlignment(Qt::AlignCenter);
    preview_->setText(QStringLiteral("将完整二维码放入画面中"));
    preview_->setStyleSheet(QStringLiteral("background: #edf3ee; border-radius: 10px; color: #3d5c49;"));
    layout->addWidget(preview_, 1);
    status_->setObjectName(QStringLiteral("qrScanStatus"));
    status_->setWordWrap(true);
    status_->setTextFormat(Qt::PlainText);
    layout->addWidget(status_);
    layout->addWidget(imageButton_);
    auto *cancel = new QPushButton(QStringLiteral("取消"), this);
    cancel->setObjectName(QStringLiteral("qrCancelButton"));
    layout->addWidget(cancel);
    if (immersive_) {
        setWindowFlags(parent ? Qt::Widget : Qt::Dialog | Qt::FramelessWindowHint);
        setModal(false);
        if (parent) setGeometry(parent->rect());
        setStyleSheet(QStringLiteral(R"QSS(
            #qrScanDialog { background:#183f34; }
            #qrScanDialog QLabel { background:transparent; border:none; }
            #qrHeader { background:rgba(24,63,52,190); border:none; border-radius:18px; }
            #qrHeader QLabel { color:#f2f5ef; font-size:18px; font-weight:600; }
            #qrCancelButton { color:#f2f5ef; background:transparent; border:none;
                              border-radius:10px; padding:0; min-height:0; font-size:18px; }
            #qrCancelButton:hover { background:#3c614d; }
            #qrScanControls { background:#f6f7f2; border:1px solid #e1e7dc; border-radius:20px; }
            #qrScanControls QLabel { color:#203d33; }
            #qrScanControls #qrScanStatus { color:#65796c; font-size:11px; }
            #qrScanControls QComboBox { color:#566f5f; background:white; border:1px solid #dce3d5;
                                       border-radius:10px; min-height:32px; padding:0 10px; font-size:11px; }
            #qrScanControls QComboBox::drop-down { border:none; width:24px; background:transparent; }
            #qrScanControls QComboBox QAbstractItemView { color:#203d33; background:white;
                                                        selection-background-color:#e3eedb; }
            #qrScanControls QPushButton { color:#36523f; background:white; border:1px solid #dce3d5;
                                         border-radius:12px; min-height:36px; padding:0 10px; font-size:12px; font-weight:600; }
            #qrScanControls QPushButton:hover { background:#edf4e5; border-color:#92ad7e; }
            #qrScanControls QPushButton:pressed { background:#e1ecd6; }
            #qrScanControls QPushButton:focus { border:2px solid #567b52; padding:0 9px; }
            #qrScanControls QPushButton:disabled { background:#edf0e8; color:#96a18e; border-color:#e1e6da; }
            #qrScanControls #qrManualButton { background:#245c45; color:white; border-color:#245c45; min-height:44px; }
            #qrScanControls #qrManualButton:hover { background:#1c4d39; }
            #qrScanControls #qrManualButton:pressed { background:#163f31; }
        )QSS"));
        while (auto *item = layout->takeAt(0)) delete item;
        preview_->setParent(cameraPage_);
        preview_->setGeometry(rect());
        preview_->lower();
        preview_->setStyleSheet(QStringLiteral("background:#183f34;color:#dbe7d2;border:none;"));

        auto *header = new QFrame(this);
        header->setObjectName("qrHeader");
        auto *top = new QHBoxLayout(header);
        top->setContentsMargins(10, 8, 16, 8);
        top->setSpacing(12);
        cancel->setText(QStringLiteral("✕"));
        cancel->setAccessibleName(QStringLiteral("关闭扫一扫"));
        cancel->setFixedSize(36,36);
        top->addWidget(cancel);
        top->addWidget(new QLabel(QStringLiteral("扫一扫"), header));
        top->addStretch();
        layout->addWidget(header);
        layout->addWidget(new ScanGuide(this), 1);

        auto *controls = new QFrame(this);
        controls->setObjectName("qrScanControls");
        auto *actions = new QVBoxLayout(controls);
        actions->setContentsMargins(16, 16, 16, 16);
        actions->setSpacing(10);
        hint->setText(QStringLiteral("对准充电桩二维码"));
        hint->setStyleSheet("font-size:15px;font-weight:600;");
        hint->setAlignment(Qt::AlignCenter);
        actions->addWidget(hint);
        status_->setAlignment(Qt::AlignCenter);
        actions->addWidget(status_);
        cameraChoice_->setMinimumContentsLength(12);
        cameraChoice_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        actions->addWidget(cameraChoice_);
        auto *manual = new QPushButton(QStringLiteral("输入电桩编号"), controls);
        manual->setObjectName("qrManualButton");
        actions->addWidget(manual);
        connect(manual,&QPushButton::clicked,this,[this]{
            stopCamera();bool ok=false;const QString code=QInputDialog::getText(this,QStringLiteral("输入电桩编号"),QStringLiteral("电桩编号"),QLineEdit::Normal,{},&ok).trimmed();
            if (finished_) return;
            if(ok){static const QRegularExpression valid("^[A-Za-z0-9][A-Za-z0-9_.-]{0,63}$");if(valid.match(code).hasMatch()){finished_=true;emit pileCodeDecoded(code);accept();return;}status_->setText(QStringLiteral("请输入有效的电桩编号"));}
            startCamera();
        });
        auto *secondary = new QHBoxLayout;
        secondary->setSpacing(10);
        imageButton_->setText(QStringLiteral("从相册识别"));
        secondary->addWidget(imageButton_, 1);
        secondary->addWidget(cameraButton_, 1);
        actions->addLayout(secondary);
        for (auto *button : {cancel, manual, imageButton_, cameraButton_})
            button->setCursor(Qt::PointingHandCursor);
        layout->addWidget(controls);
        layout->setContentsMargins(16, 16, 16, 20);
        layout->setSpacing(12);
    }
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(imageButton_, &QPushButton::clicked, this, &QrScanDialog::chooseImage);
    connect(cameraButton_, &QPushButton::clicked, this, [this] {
        if (camera_) { stopCamera(); status_->setText(QStringLiteral("摄像头已停止")); }
        else startCamera();
    });
    connect(cameraChoice_, &QComboBox::currentIndexChanged, this, [this] {
        if (camera_) startCamera();
    });
    connect(devices_, &QMediaDevices::videoInputsChanged, this, &QrScanDialog::refreshCameras);
    capture_->setVideoSink(sink_);
    frameWatchdog_->setInterval(5000);
    connect(frameWatchdog_, &QTimer::timeout, this, [this] {
        if (camera_ && lastFrameClock_.elapsed() >= 5000) {
            preview_->clear();
            preview_->setText(QStringLiteral("暂未收到摄像头画面"));
            status_->setText(QStringLiteral("摄像头已连接但没有传来画面，请切换设备，或检查虚拟机摄像头连接及其他程序占用。"));
        }
    });
    connect(sink_, &QVideoSink::videoFrameChanged, this, [this](const QVideoFrame &frame) {
        if (!camera_ || finished_ || !isVisible()) return;
        const QImage image = frame.toImage();
        if (image.isNull()) return;
        lastFrameClock_.restart();
        preview_->setPixmap(QPixmap::fromImage(image).scaled(preview_->size(), immersive_ ? Qt::KeepAspectRatioByExpanding : Qt::KeepAspectRatio,
                                                           Qt::SmoothTransformation));
        if (!busy_ && frameClock_.elapsed() >= 250) {
            frameClock_.restart();
            decode(image);
        }
    });
    worker_->moveToThread(&workerThread_);
    connect(&workerThread_, &QThread::finished, worker_, &QObject::deleteLater);
    workerThread_.start();
    refreshCameras();
    if (source == Source::Camera) {
        const quint64 generation = generation_;
        QTimer::singleShot(0, this, [this, generation] {
            if (!finished_ && isVisible() && generation == generation_) startCamera();
        });
    }
}

QrScanDialog::~QrScanDialog()
{
    finished_ = true;
    stopCamera();
    workerThread_.quit();
    workerThread_.wait();
}

void QrScanDialog::refreshCameras()
{
    const auto cameras = QMediaDevices::videoInputs();
    const auto selected = camera_ ? camera_->cameraDevice()
                                  : cameraChoice_->currentData().value<QCameraDevice>();
    const int selectedIndex = cameras.indexOf(selected);
    // Enumeration may finish after startup, or an unrelated camera may change.
    // Keep the selected stream and image recognition alive unless it disappeared.
    if (camera_ && selectedIndex < 0) stopCamera();
    cameraButton_->setText(camera_ ? (immersive_ ? QStringLiteral("暂停画面") : QStringLiteral("停止摄像头"))
                                   : (immersive_ ? QStringLiteral("开启摄像头") : QStringLiteral("开始摄像头扫码")));
    const QSignalBlocker blocker(cameraChoice_);
    cameraChoice_->clear();
    int preferred = -1;
    for (const auto &camera : cameras) {
        bool color = false;
        for (const auto &format : camera.videoFormats())
            if (format.pixelFormat() != QVideoFrameFormat::Format_Y8
                && format.pixelFormat() != QVideoFrameFormat::Format_Y16) color = true;
        const QString label = immersive_
            ? QStringLiteral("%1 · %2").arg(camera.description().section(':',0,0), color ? QStringLiteral("彩色") : QStringLiteral("灰度"))
            : QStringLiteral("%1 · %2%3")
            .arg(camera.description(), color ? QStringLiteral("彩色") : QStringLiteral("灰度"),
                 camera.id().isEmpty() ? QString() : QStringLiteral(" (%1)").arg(QString::fromUtf8(camera.id())));
        if (color && preferred < 0) preferred = cameraChoice_->count();
        cameraChoice_->addItem(label, QVariant::fromValue(camera));
    }
    if (selectedIndex >= 0) cameraChoice_->setCurrentIndex(selectedIndex);
    else if (preferred >= 0) cameraChoice_->setCurrentIndex(preferred);
    cameraChoice_->setEnabled(!cameras.isEmpty());
    cameraButton_->setEnabled(!cameras.isEmpty());
    if (camera_ || busy_) return;
    if (cameras.isEmpty())
        status_->setText(QStringLiteral("未检测到摄像头。请连接 USB 摄像头，或选择二维码图片。"));
    else status_->setText(QStringLiteral("选择摄像头后开始扫码，也可以识别图片。"));
}

void QrScanDialog::startCamera()
{
    stopCamera();
    pendingImagePath_.clear();
    if (finished_ || !isVisible()) return;
    const auto device = cameraChoice_->currentData().value<QCameraDevice>();
    if (!device.isNull()) {
        camera_ = new QCamera(device, this);
        // A modest compressed stream avoids high USB bandwidth in virtual machines.
        for (const auto &format : device.videoFormats()) {
            if (format.resolution() == QSize(640, 480)
                && format.pixelFormat() == QVideoFrameFormat::Format_Jpeg) {
                camera_->setCameraFormat(format);
                break;
            }
        }
        connect(camera_, &QCamera::errorOccurred, this, [this](QCamera::Error, const QString &) {
            stopCamera();
            status_->setText(QStringLiteral("无法使用摄像头，请检查权限、设备连接或是否被其他程序占用；也可识别图片。"));
        });
        capture_->setCamera(camera_);
        frameClock_.start();
        lastFrameClock_.start();
        frameWatchdog_->start();
        preview_->setText(QStringLiteral("正在等待摄像头画面…"));
        cameraButton_->setText((immersive_ ? QStringLiteral("暂停画面") : QStringLiteral("停止摄像头")));
        status_->setText(QStringLiteral("正在启动摄像头…"));
        camera_->start();
        return;
    }
    refreshCameras();
}

void QrScanDialog::stopCamera()
{
    ++generation_;
    frameWatchdog_->stop();
    if (camera_) {
        auto *camera = camera_;
        camera_ = nullptr;
        camera->disconnect(this);
        camera->stop();
        capture_->setCamera(nullptr);
        camera->deleteLater();
    }
    cameraButton_->setText((immersive_ ? QStringLiteral("开启摄像头") : QStringLiteral("开始摄像头扫码")));
    preview_->clear();
    preview_->setText(QStringLiteral("将完整二维码放入画面中"));
}

void QrScanDialog::chooseImage()
{
    if (finished_) return;
    resumeCameraAfterAlbum_ = camera_ != nullptr;
    stopCamera();
    pendingImagePath_.clear();
    if (!album_) {
        album_ = new PhotoAlbumPage(this);
        pages_->addWidget(album_);
        connect(album_, &PhotoAlbumPage::backRequested, this, [this] {
            pages_->setCurrentWidget(cameraPage_);
            if (resumeCameraAfterAlbum_ && !finished_) startCamera();
        });
        connect(album_, &PhotoAlbumPage::imageSelected, this, [this](const QString &path) {
            if (finished_) return;
            pages_->setCurrentWidget(cameraPage_);
            readImageFile(path);
        });
    }
    album_->reload();
    pages_->setCurrentWidget(album_);
    album_->setFocus();
}

void QrScanDialog::readImageFile(const QString &path)
{
    if (finished_) return;
    stopCamera();
    status_->setText(QStringLiteral("正在识别图片…"));
    if (busy_) {
        pendingImagePath_ = path;
        return;
    }
    decode({}, path);
}

void QrScanDialog::decode(const QImage &image, const QString &path)
{
    if (busy_ || finished_) return;
    busy_ = true;
    // Live-frame decoding must not make the image action flicker or miss clicks.
    imageButton_->setEnabled(path.isEmpty());
    const quint64 generation = generation_;
    // A single worker and one outstanding frame prevent camera backlog/UI stalls.
    QMetaObject::invokeMethod(worker_, [this, image, path, generation] {
        const auto result = path.isEmpty() ? decodePileQr(image) : decodePileQrFile(path);
        QMetaObject::invokeMethod(this, [this, result, generation] { decoded(result, generation); },
                                  Qt::QueuedConnection);
    }, Qt::QueuedConnection);
}

void QrScanDialog::decoded(const QrDecodeResult &result, quint64 generation)
{
    busy_ = false;
    imageButton_->setEnabled(true);
    if (finished_ || !isVisible()) return;
    if (!pendingImagePath_.isEmpty()) {
        const QString path = pendingImagePath_;
        pendingImagePath_.clear();
        decode({}, path);
        return;
    }
    if (generation != generation_) return;
    if (!result.ok()) {
        status_->setText(immersive_ && camera_ && result.error.contains(QStringLiteral("未发现"))
            ? QStringLiteral("识别后前往充电页，确认后开始充电") : result.error);
        return;
    }
    finished_ = true;
    stopCamera();
    emit pileCodeDecoded(result.pileCode);
    accept();
}

void QrScanDialog::done(int result)
{
    finished_ = true;
    stopCamera();
    QDialog::done(result);
}

void QrScanDialog::resizeEvent(QResizeEvent *event)
{
    QDialog::resizeEvent(event);
    if (immersive_) { preview_->setGeometry(rect()); preview_->lower(); }
}

void QrScanDialog::hideEvent(QHideEvent *event)
{
    finished_ = true;
    stopCamera();
    QDialog::hideEvent(event);
}

} // namespace charging::client
