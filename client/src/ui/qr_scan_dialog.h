#pragma once

#include "local/qr_decoder.h"
#include <QDialog>
#include <QElapsedTimer>
#include <QThread>

class QCamera;
class QComboBox;
class QLabel;
class QMediaCaptureSession;
class QMediaDevices;
class QPushButton;
class QVideoSink;
class QTimer;
class QStackedLayout;

namespace charging::client {

class PhotoAlbumPage;

class QrScanDialog final : public QDialog {
    Q_OBJECT
public:
    enum class Source { Camera, Image };
    explicit QrScanDialog(Source source, QWidget *parent = nullptr, bool immersive = false);
    ~QrScanDialog() override;
    void readImageFile(const QString &path);
    void chooseImage();
    void done(int result) override;

signals:
    void pileCodeDecoded(const QString &pileCode);

protected:
    void resizeEvent(QResizeEvent *event) override;
    void hideEvent(QHideEvent *event) override;

private:
    void refreshCameras();
    void startCamera();
    void stopCamera();
    void decode(const QImage &image, const QString &path = {});
    void decoded(const QrDecodeResult &result, quint64 generation);

    QStackedLayout *pages_ = nullptr;
    QWidget *cameraPage_ = nullptr;
    PhotoAlbumPage *album_ = nullptr;
    bool resumeCameraAfterAlbum_ = false;
    QMediaDevices *devices_;
    QMediaCaptureSession *capture_;
    QVideoSink *sink_;
    QCamera *camera_ = nullptr;
    QComboBox *cameraChoice_;
    QPushButton *cameraButton_;
    QPushButton *imageButton_;
    QLabel *preview_;
    QLabel *status_;
    QThread workerThread_;
    QObject *worker_;
    QElapsedTimer frameClock_;
    QElapsedTimer lastFrameClock_;
    QTimer *frameWatchdog_;
    quint64 generation_ = 0;
    bool immersive_ = false;
    bool busy_ = false;
    bool finished_ = false;
    QString pendingImagePath_;
};

} // namespace charging::client
