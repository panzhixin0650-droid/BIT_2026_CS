// 本文件测试头像本地存储的保存、归一化与路径恢复
#include "local/avatar_storage.h"

#include <QFileInfo>
#include <QImage>
#include <QTemporaryDir>
#include <QtTest>

using charging::client::AvatarStorage;

// 头像存储测试集合
class AvatarStorageTests : public QObject {
    Q_OBJECT

private slots:
    void savesNormalizedAvatarAndRestoresPath();
    void rejectsUnreadableImage();
};

// 保存后限制在 512 像素内，并记录可查询的文件路径
void AvatarStorageTests::savesNormalizedAvatarAndRestoresPath()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());
    const QString sourcePath = temporaryDirectory.filePath(QStringLiteral("source.jpg"));
    QImage source(800, 600, QImage::Format_RGB32);
    source.fill(Qt::blue);
    QVERIFY(source.save(sourcePath));

    const QString dataDirectory = temporaryDirectory.filePath(QStringLiteral("data"));
    const QString settingsFile = temporaryDirectory.filePath(QStringLiteral("settings.ini"));
    AvatarStorage storage(dataDirectory, settingsFile);
    QString savedPath;
    QString error;

    const QString userKey = QStringLiteral("42:13800000001");
    QVERIFY(storage.saveAvatar(userKey, sourcePath, &savedPath, &error));
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QVERIFY(QFileInfo::exists(savedPath));
    QVERIFY(savedPath.contains(QStringLiteral("avatars/user-")));
    QVERIFY(savedPath.endsWith(QStringLiteral(".png")));
    QCOMPARE(storage.avatarPath(userKey), savedPath);

    const QImage storedImage(savedPath);
    QVERIFY(!storedImage.isNull());
    QVERIFY(storedImage.width() <= 512);
    QVERIFY(storedImage.height() <= 512);

    // 重新构造存储对象仍能读回上次的头像路径
    AvatarStorage restoredStorage(dataDirectory, settingsFile);
    QCOMPARE(restoredStorage.avatarPath(userKey), savedPath);
}

// 不可读图片被拒绝，且不留下任何记录
void AvatarStorageTests::rejectsUnreadableImage()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());
    AvatarStorage storage(temporaryDirectory.filePath(QStringLiteral("data")),
                          temporaryDirectory.filePath(QStringLiteral("settings.ini")));
    QString error;

    QVERIFY(!storage.saveAvatar(QStringLiteral("1:13800000001"),
                                temporaryDirectory.filePath(QStringLiteral("missing.png")),
                                nullptr,
                                &error));
    QVERIFY(!error.isEmpty());
    QVERIFY(storage.avatarPath(QStringLiteral("1:13800000001")).isEmpty());
}

QTEST_GUILESS_MAIN(AvatarStorageTests)

#include "avatar_storage_tests.moc"
