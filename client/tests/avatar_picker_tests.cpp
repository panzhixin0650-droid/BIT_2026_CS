// 本文件测试头像选择对话框与本地头像存储的配合
#include "api/mock_charging_api.h"
#include "local/avatar_storage.h"
#include "ui/avatar_art.h"
#include "ui/avatar_picker_dialog.h"
#include "ui/client_theme.h"
#include "ui/profile_controller.h"
#include "ui/profile_page.h"

#include <QFile>
#include <QFileDialog>
#include <QLabel>
#include <QPushButton>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

using namespace charging::client;

// 头像选择测试集合
class AvatarPickerTests : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() { QApplication::setAttribute(Qt::AA_DontUseNativeDialogs); }
    void basicAvatarsPersistAndRefreshImmediately();
    void localImagePreviewAndInvalidSelection();
    void cancellationAndAccountChangeDiscardSelection();
    void smallWindowAndHighDpiRendering();
};

// 内置头像保存到本地并立即刷新显示，切换账号各自独立
void AvatarPickerTests::basicAvatarsPersistAndRefreshImmediately()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    AvatarStorage storage(dir.filePath("data"), dir.filePath("settings.ini"));
    MockChargingApi api;
    ProfilePage page;
    ProfileController controller(page, api, storage);
    charging::protocol::UserDto user;
    user.userId = 42;
    user.phone = QStringLiteral("13800000001");
    controller.setInitialUser(user);
    page.show();
    QSignalSpy profileRequests(&api, &IChargingApi::profileUpdateCompleted);
    QSignalSpy selected(&page, &ProfilePage::avatarSelected);
    const QString key = QStringLiteral("42:13800000001");
    for (int i = 0; i < 4; ++i) {
        page.findChild<QPushButton *>("changeAvatarButton")->click();
        auto *picker = page.findChild<AvatarPickerDialog *>();
        QVERIFY(picker);
        auto *confirm = picker->findChild<QPushButton *>("confirmAvatarButton");
        QVERIFY(!confirm->isEnabled());
        picker->findChild<QPushButton *>(QStringLiteral("basicAvatar%1").arg(i))->click();
        QCOMPARE(selected.count(), i);
        QVERIFY(confirm->isEnabled());
        confirm->click();
        QCOMPARE(selected.count(), i + 1);
        const QImage saved(storage.avatarPath(key));
        QCOMPARE(saved.convertToFormat(QImage::Format_ARGB32),
                 basicAvatars()[i].image.convertToFormat(QImage::Format_ARGB32));
        const auto actual = page.findChild<QLabel *>("profileAvatar")->pixmap().toImage();
        QCOMPARE(actual, circularAvatar(saved, 64, page.devicePixelRatioF()).toImage());
        QTRY_VERIFY(page.findChild<AvatarPickerDialog *>() == nullptr);
    }
    AvatarStorage restored(dir.filePath("data"), dir.filePath("settings.ini"));
    QCOMPARE(QImage(restored.avatarPath(key)).convertToFormat(QImage::Format_ARGB32),
             basicAvatars()[3].image.convertToFormat(QImage::Format_ARGB32));
    user.userId = 43;
    controller.setInitialUser(user);
    QVERIFY(storage.avatarPath(QStringLiteral("43:13800000001")).isEmpty());
    QCOMPARE(page.findChild<QLabel *>("profileAvatar")->pixmap().toImage(),
             circularAvatar(defaultAvatar(), 64, page.devicePixelRatioF()).toImage());
    user.userId = 42;
    controller.setInitialUser(user);
    QCOMPARE(page.findChild<QLabel *>("profileAvatar")->pixmap().toImage(),
             circularAvatar(basicAvatars()[3].image, 64, page.devicePixelRatioF()).toImage());
    QTest::qWait(20);
    QCOMPARE(profileRequests.count(), 0);
}

// 本地图片预览、缺失或损坏文件的提示与缩放上限
void AvatarPickerTests::localImagePreviewAndInvalidSelection()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QImage source(900, 600, QImage::Format_RGB32);
    source.fill(Qt::blue);
    const QString path = dir.filePath("local.png");
    QVERIFY(source.save(path));
    AvatarPickerDialog picker(basicAvatars()[0].image);
    picker.show();
    auto *confirm = picker.findChild<QPushButton *>("confirmAvatarButton");
    auto *error = picker.findChild<QLabel *>("avatarErrorLabel");
    picker.selectLocalImage(dir.filePath("missing.png"));
    QVERIFY(error->isVisible());
    QVERIFY(!confirm->isEnabled());
    auto *basic = picker.findChild<QPushButton *>("basicAvatar1");
    basic->click();
    QVERIFY(basic->isChecked());
    picker.findChild<QPushButton *>("localAvatarButton")->click();
    auto *files = picker.findChild<QFileDialog *>();
    QVERIFY(files);
    files->reject();
    QVERIFY(basic->isChecked());
    QTRY_VERIFY(picker.findChild<QFileDialog *>() == nullptr);
    picker.findChild<QPushButton *>("localAvatarButton")->click();
    files = picker.findChild<QFileDialog *>();
    QVERIFY(files);
    files->selectFile(path);
    QVERIFY(QMetaObject::invokeMethod(files, "accept", Qt::DirectConnection));
    QVERIFY(!basic->isChecked());
    QVERIFY(error->isHidden());
    const QImage expected = source.scaled(512, 512, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    QCOMPARE(picker.selectedImage(), expected);
    QCOMPARE(picker.findChild<QLabel *>("avatarPreview")->pixmap().toImage(),
             circularAvatar(expected, 88, picker.devicePixelRatioF()).toImage());
    picker.selectLocalImage({});
    QCOMPARE(picker.selectedImage(), expected);
    const QString corruptPath = dir.filePath("corrupt.png");
    QFile corrupt(corruptPath);
    QVERIFY(corrupt.open(QIODevice::WriteOnly));
    corrupt.write("not an image");
    corrupt.close();
    picker.selectLocalImage(corruptPath);
    QVERIFY(error->isVisible());
    QCOMPARE(picker.selectedImage(), expected);
    QVERIFY(confirm->isEnabled());

    AvatarStorage storage(dir.filePath("data"), dir.filePath("settings.ini"));
    QString savedPath;
    QVERIFY(storage.saveImage(QStringLiteral("user"), picker.selectedImage(), &savedPath));
    QVERIFY(QFile::remove(path));
    QCOMPARE(QImage(savedPath), expected);
    QString errorMessage;
    QVERIFY(!storage.saveImage(QStringLiteral("user"), QImage(), nullptr, &errorMessage));
    QVERIFY(!errorMessage.isEmpty());
    QCOMPARE(QImage(savedPath), expected);
}

// 取消、Esc、关闭或切换账号都要放弃本次选择
void AvatarPickerTests::cancellationAndAccountChangeDiscardSelection()
{
    ProfilePage page;
    charging::protocol::UserDto user;
    user.userId = 42;
    page.setUser(user);
    page.show();
    QSignalSpy selected(&page, &ProfilePage::avatarSelected);
    auto *change = page.findChild<QPushButton *>("changeAvatarButton");
    for (int action = 0; action < 4; ++action) {
        change->click();
        auto *picker = page.findChild<AvatarPickerDialog *>();
        QVERIFY(picker);
        picker->findChild<QPushButton *>("basicAvatar2")->click();
        if (action == 0) {
            picker->findChild<QPushButton *>("cancelAvatarButton")->click();
        } else if (action == 1) {
            QTest::keyClick(picker, Qt::Key_Escape);
        } else if (action == 2) {
            picker->close();
        } else {
            page.setUser(charging::protocol::UserDto{});
        }
        QTRY_VERIFY(page.findChild<AvatarPickerDialog *>() == nullptr);
        QCOMPARE(selected.count(), 0);
    }
}

// 小窗口下控件不越界，高 DPI 圆形头像尺寸正确
void AvatarPickerTests::smallWindowAndHighDpiRendering()
{
    AvatarPickerDialog picker(basicAvatars()[0].image);
    picker.setStyleSheet(clientThemeStyleSheet() + picker.styleSheet());
    picker.resize(320, 540);
    picker.show();
    QTest::qWait(20);
    QVERIFY(picker.width() <= 360);
    QVERIFY(picker.height() <= 600);
    for (auto *button : picker.findChildren<QPushButton *>()) {
        QVERIFY2(picker.rect().contains(button->geometry()), qPrintable(button->objectName()));
    }
    picker.selectLocalImage(QStringLiteral("/missing-avatar.png"));
    QTest::qWait(20);
    QVERIFY(picker.width() <= 360);
    QVERIFY(picker.height() <= 600);
    const QPixmap pixmap = circularAvatar(basicAvatars()[0].image, 64, 2);
    QCOMPARE(pixmap.size(), QSize(128, 128));
    QCOMPARE(pixmap.devicePixelRatio(), 2.0);
    QCOMPARE(pixmap.toImage().pixelColor(0, 0).alpha(), 0);
}

QTEST_MAIN(AvatarPickerTests)
#include "avatar_picker_tests.moc"
