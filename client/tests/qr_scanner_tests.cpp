#include "local/qr_decoder.h"
#include "ui/qr_scan_dialog.h"
#include "ui/scan_page.h"
#include "ui/photo_album_page.h"
#include <QListWidget>
#include <QDir>
#include <QTemporaryDir>

#include <QImage>
#include <QCameraDevice>
#include <QCamera>
#include <QComboBox>
#include <QLabel>
#include <QInputDialog>
#include <QTimer>
#include <QLineEdit>
#include <QMediaDevices>
#include <QPainter>
#include <QPushButton>
#include <QSignalSpy>
#include <QTemporaryFile>
#include <QVideoFrame>
#include <QVideoFrameFormat>
#include <QVideoSink>
#include <QtTest>
#include <cstring>

using namespace charging::client;

namespace {
QString fixture(const QString &name) { return QStringLiteral(QR_FIXTURE_DIR) + '/' + name + ".png"; }
}

class QrScannerTests final : public QObject {
    Q_OBJECT
private slots:
    void albumSelectsRealQrAndCanPreview();
    void albumCancellationAndInvalidPhoto();
    void bundledAlbumContainsReadableQrImages();
    void readsQrPixelsIncludingRotationAndPaddedRows();
    void readsPixelsConvertedFromVideoFrame();
    void rejectsMissingInvalidAndAmbiguousImages();
    void recognitionSelectsPileWithoutStartingOrder();
    void immersiveCameraHasManualEntry();
    void cancelDiscardsPendingRecognition();
    void leavingPageClosesScanner();
    void noCameraStillAllowsImages();
    void liveCameraKeepsStreamingAfterDeviceRefresh();
};

void QrScannerTests::albumSelectsRealQrAndCanPreview()
{
    ScanPage page;page.resize(360,640);page.show();
    QSignalSpy selected(&page,&ScanPage::scanRequested);
    QTRY_VERIFY(page.findChild<QrScanDialog *>());
    QPointer<QrScanDialog> dialog=page.findChild<QrScanDialog *>();
    dialog->chooseImage();
    auto *album=dialog->findChild<PhotoAlbumPage *>();
    QVERIFY(album && !album->isWindow());
    QTRY_VERIFY(album->isVisible());
    QTRY_COMPARE(album->size(),dialog->size());
    auto *photos=album->findChild<QListWidget *>("albumPhotos");
    QVERIFY(photos->count()>=6);
    auto *confirm=album->findChild<QPushButton *>("albumConfirm");
    QVERIFY(!confirm->isEnabled());
    QTRY_VERIFY(photos->visualItemRect(photos->item(0)).height()>80);
    QTRY_COMPARE(photos->visualItemRect(photos->item(0)).top(), photos->visualItemRect(photos->item(3)).top());
    QTest::mouseClick(photos->viewport(),Qt::LeftButton,Qt::NoModifier,
                      photos->visualItemRect(photos->item(0)).center());
    QVERIFY(confirm->isEnabled()); QCOMPARE(selected.count(),0);
    const auto screenshots=qEnvironmentVariable("CHARGING_FLOW_SCREENSHOTS");
    if(!screenshots.isEmpty())album->grab().save(QDir(screenshots).filePath("album-360.png"));
    album->findChild<QPushButton *>("albumPreview")->click();
    auto *preview=album->findChild<QLabel *>("albumPreviewImage");
    QTRY_VERIFY(preview->isVisible());
    QVERIFY(!preview->pixmap(Qt::ReturnByValue).isNull());
    if(!screenshots.isEmpty())album->grab().save(QDir(screenshots).filePath("album-preview-360.png"));
    album->findChild<QPushButton *>("albumBack")->click();
    QVERIFY(photos->isVisible());
    confirm->click();
    QTRY_COMPARE(selected.count(),1);
    QCOMPARE(selected.first().first().toString(),QStringLiteral("PILE-A-01"));
    QTRY_VERIFY(dialog.isNull());
}

void QrScannerTests::albumCancellationAndInvalidPhoto()
{
    QTemporaryDir folder;QVERIFY(folder.isValid());
    QImage blank(100,100,QImage::Format_RGB32);blank.fill(Qt::white);
    QVERIFY(blank.save(folder.filePath("blank.png")));
    const QByteArray previous=qgetenv("CHARGING_DEMO_ALBUM_DIR");
    qputenv("CHARGING_DEMO_ALBUM_DIR",folder.path().toUtf8());
    QWidget parent;parent.resize(360,640);parent.show();
    QrScanDialog dialog(QrScanDialog::Source::Image,&parent,true);dialog.show();
    QSignalSpy selected(&dialog,&QrScanDialog::pileCodeDecoded);
    dialog.chooseImage();
    auto *album=dialog.findChild<PhotoAlbumPage *>();
    auto *photos=album->findChild<QListWidget *>("albumPhotos");
    QCOMPARE(photos->count(),1);
    album->findChild<QPushButton *>("albumBack")->click();
    QVERIFY(!album->isVisible());QVERIFY(dialog.isVisible());QCOMPARE(selected.count(),0);
    dialog.chooseImage();
    if(previous.isNull())qunsetenv("CHARGING_DEMO_ALBUM_DIR");else qputenv("CHARGING_DEMO_ALBUM_DIR",previous);
    photos->setCurrentRow(0);
    album->findChild<QPushButton *>("albumConfirm")->click();
    QTRY_VERIFY(dialog.findChild<QLabel *>("qrScanStatus")->text().contains(QStringLiteral("未发现")));
    QCOMPARE(selected.count(),0);QVERIFY(dialog.isVisible());
    dialog.chooseImage();
    dialog.reject();QVERIFY(!album->isVisible());
}

void QrScannerTests::bundledAlbumContainsReadableQrImages()
{
    PhotoAlbumPage album(nullptr,QStringLiteral(":/demo-album"));album.reload();
    auto *photos=album.findChild<QListWidget *>("albumPhotos");
    QCOMPARE(photos->count(),9);
    int qrCount=0, ordinaryCount=0;
    for(int i=0;i<photos->count();++i){
        const auto *item=photos->item(i);
        const auto result=decodePileQrFile(item->data(Qt::UserRole).toString());
        if(item->text().startsWith(QStringLiteral("PILE-"))){
            QCOMPARE(result.pileCode,item->text());
            ++qrCount;
        }else{
            QVERIFY(!result.ok());
            QVERIFY(result.error.contains(QStringLiteral("未发现")));
            ++ordinaryCount;
        }
    }
    QCOMPARE(qrCount,6);
    QCOMPARE(ordinaryCount,3);
    QTemporaryDir empty;QVERIFY(empty.isValid());
    PhotoAlbumPage emptyAlbum(nullptr,empty.path());emptyAlbum.reload();
    QCOMPARE(emptyAlbum.findChild<QListWidget *>("albumPhotos")->count(),0);
    QVERIFY(!emptyAlbum.findChild<QPushButton *>("albumConfirm")->isEnabled());
}

void QrScannerTests::readsQrPixelsIncludingRotationAndPaddedRows()
{
    const QImage qr(fixture("PILE-A-01"));
    QVERIFY(!qr.isNull());
    QCOMPARE(decodePileQr(qr).pileCode, QStringLiteral("PILE-A-01"));
    QCOMPARE(decodePileQr(qr.transformed(QTransform().rotate(90))).pileCode, QStringLiteral("PILE-A-01"));
    QImage padded(qr.width() + 7, qr.height() + 16, QImage::Format_Grayscale8);
    padded.fill(Qt::white);
    { QPainter painter(&padded); painter.drawImage(3, 8, qr); }
    QCOMPARE(decodePileQr(padded).pileCode, QStringLiteral("PILE-A-01"));
    QCOMPARE(decodePileQrFile(fixture("PILE-B-02")).pileCode, QStringLiteral("PILE-B-02"));
}

void QrScannerTests::rejectsMissingInvalidAndAmbiguousImages()
{
    QVERIFY(!decodePileQr({}).ok());
    QVERIFY(!decodePileQrFile(fixture("missing")).ok());
    QTemporaryFile invalid;
    QVERIFY(invalid.open());
    invalid.write("not an image"); invalid.flush();
    QVERIFY(!decodePileQrFile(invalid.fileName()).ok());
    QVERIFY(invalid.resize(21 * 1024 * 1024));
    QVERIFY(decodePileQrFile(invalid.fileName()).error.contains(QStringLiteral("20 MB")));
    QImage blank(400, 400, QImage::Format_RGB32); blank.fill(Qt::white);
    QVERIFY(decodePileQr(blank).error.contains(QStringLiteral("未发现")));
    QVERIFY(!decodePileQrFile(fixture("invalid-url")).ok());
    QImage first(fixture("PILE-A-01")), second(fixture("PILE-B-02"));
    QImage two(first.width() + second.width() + 40, first.height(), QImage::Format_RGB32);
    two.fill(Qt::white);
    { QPainter painter(&two); painter.drawImage(0, 0, first); painter.drawImage(first.width() + 40, 0, second); }
    QVERIFY(decodePileQr(two).error.contains(QStringLiteral("多个")));
}

void QrScannerTests::readsPixelsConvertedFromVideoFrame()
{
    const QImage qr = QImage(fixture("PILE-A-01")).convertToFormat(QImage::Format_RGBA8888);
    QVideoFrame frame(QVideoFrameFormat(qr.size(), QVideoFrameFormat::Format_RGBA8888));
    QVERIFY(frame.map(QVideoFrame::WriteOnly));
    for (int y = 0; y < qr.height(); ++y)
        std::memcpy(frame.bits(0) + y * frame.bytesPerLine(0), qr.constScanLine(y), qr.width() * 4);
    frame.unmap();
    QCOMPARE(decodePileQr(frame.toImage()).pileCode, QStringLiteral("PILE-A-01"));
}

void QrScannerTests::recognitionSelectsPileWithoutStartingOrder()
{
    ScanPage page; page.resize(360, 640); page.show();
    QSignalSpy charge(&page, &ScanPage::scanRequested), repair(&page, &ScanPage::repairRequested);
    page.findChild<QPushButton *>("scanCameraButton")->click();
    auto *dialog = page.findChild<QrScanDialog *>();
    QVERIFY(dialog);
    // Run the real file decoder and the production dialog -> page connection.
    // Select the image before deferred camera startup, so this flow is independent
    // of physical camera availability and driver behavior.
    dialog->readImageFile(fixture("PILE-A-01"));
    // A late device-enumeration/hotplug notification must not discard the image.
    QVERIFY(QMetaObject::invokeMethod(dialog->findChild<QMediaDevices *>(),
                                    "videoInputsChanged", Qt::DirectConnection));
    auto *input = page.findChild<QLineEdit *>("scanPileCodeInput");
    QTRY_COMPARE(input->text(), QStringLiteral("PILE-A-01"));
    QTRY_COMPARE(charge.count(), 1); // Selection only; MainWindow opens charging confirmation.
    QCOMPARE(charge.at(0).at(0).toString(), QStringLiteral("PILE-A-01"));
    QCOMPARE(repair.count(), 0);

}

void QrScannerTests::immersiveCameraHasManualEntry()
{
    ScanPage page; page.resize(360,640); page.show();
    QSignalSpy selected(&page,&ScanPage::scanRequested);
    QTRY_VERIFY(page.findChild<QrScanDialog *>());
    QPointer<QrScanDialog> dialog=page.findChild<QrScanDialog *>();
    QCOMPARE(dialog->size(),page.size());
    QCOMPARE(dialog->findChild<QLabel *>("qrCameraPreview")->geometry(),dialog->rect());
    auto *manual=dialog->findChild<QPushButton *>("qrManualButton");
    QVERIFY(manual && manual->isVisible());
    QTimer::singleShot(20,&page,[&]{
        auto *input=dialog->findChild<QInputDialog *>();
        QVERIFY(input);input->setTextValue("PILE-B-02");input->accept();
    });
    manual->click();
    QTRY_COMPARE(selected.count(),1);
    QCOMPARE(selected.first().first().toString(),QStringLiteral("PILE-B-02"));
    QTRY_VERIFY(dialog.isNull());
}

void QrScannerTests::cancelDiscardsPendingRecognition()
{
    QrScanDialog dialog(QrScanDialog::Source::Image);
    QSignalSpy results(&dialog, &QrScanDialog::pileCodeDecoded);
    dialog.show();
    dialog.readImageFile(fixture("PILE-A-01"));
    dialog.reject();
    QTest::qWait(200);
    QCOMPARE(results.count(), 0);
}

void QrScannerTests::leavingPageClosesScanner()
{
    ScanPage page; page.show();
    page.findChild<QPushButton *>("scanCameraButton")->click();
    QPointer<QrScanDialog> dialog = page.findChild<QrScanDialog *>();
    QVERIFY(dialog);
    dialog->readImageFile(fixture("PILE-A-01"));
    page.reset(); // Logout must also invalidate in-flight recognition.
    QTRY_VERIFY(dialog.isNull());
    QCOMPARE(page.findChild<QLineEdit *>("scanPileCodeInput")->text(), QString());
    page.findChild<QPushButton *>("scanCameraButton")->click();
    dialog = page.findChild<QrScanDialog *>();
    QVERIFY(dialog);
    page.hide();
    QTRY_VERIFY(dialog.isNull());
}

void QrScannerTests::noCameraStillAllowsImages()
{
    if (!QMediaDevices::videoInputs().isEmpty()) QSKIP("No-camera case requires no attached video input");
    QrScanDialog dialog(QrScanDialog::Source::Camera); dialog.show();
    QVERIFY(!dialog.findChild<QPushButton *>("qrCameraButton")->isEnabled());
    QVERIFY(dialog.findChild<QPushButton *>("qrImageButton")->isEnabled());
    QVERIFY(dialog.findChild<QLabel *>("qrScanStatus")->text().contains(QStringLiteral("未检测到")));
}

void QrScannerTests::liveCameraKeepsStreamingAfterDeviceRefresh()
{
    if (qEnvironmentVariableIntValue("CHARGING_TEST_CAMERA") != 1)
        QSKIP("Set CHARGING_TEST_CAMERA=1 to verify an attached physical camera");
    QVERIFY(!QMediaDevices::videoInputs().isEmpty());
    QWidget parent; parent.resize(480,860); parent.show();
    QrScanDialog dialog(QrScanDialog::Source::Camera,&parent,true);
    int frames = 0;
    connect(dialog.findChild<QVideoSink *>(), &QVideoSink::videoFrameChanged,
            &dialog, [&](const QVideoFrame &frame) { if (frame.isValid()) ++frames; });
    dialog.show();
    QTRY_VERIFY_WITH_TIMEOUT(frames >= 5, 5000);
    auto *preview = dialog.findChild<QLabel *>("qrCameraPreview");
    QVERIFY(!preview->pixmap(Qt::ReturnByValue).isNull());
    QPointer<QCamera> camera = dialog.findChild<QCamera *>();
    QVERIFY(camera && camera->isActive());
    const auto selected = dialog.findChild<QComboBox *>("qrCameraChoice")
                              ->currentData().value<QCameraDevice>();
    const int previousFrames = frames;
    QVERIFY(QMetaObject::invokeMethod(dialog.findChild<QMediaDevices *>(),
                                    "videoInputsChanged", Qt::DirectConnection));
    QCOMPARE(dialog.findChild<QComboBox *>("qrCameraChoice")
                 ->currentData().value<QCameraDevice>(), selected);
    QVERIFY(camera && camera->isActive());
    QTRY_VERIFY_WITH_TIMEOUT(frames >= previousFrames + 5, 5000);
    dialog.chooseImage();
    QVERIFY(camera.isNull() || !camera->isActive());
    const int beforeAlbumReturn=frames;
    dialog.findChild<PhotoAlbumPage *>()->findChild<QPushButton *>("albumBack")->click();
    QTRY_VERIFY_WITH_TIMEOUT(frames >= beforeAlbumReturn + 5, 5000);
    QPointer<QCamera> resumed=dialog.findChild<QCamera *>();
    QVERIFY(resumed && resumed->isActive());
    dialog.reject();
    QVERIFY(resumed.isNull() || !resumed->isActive());
}

QTEST_MAIN(QrScannerTests)
#include "qr_scanner_tests.moc"
