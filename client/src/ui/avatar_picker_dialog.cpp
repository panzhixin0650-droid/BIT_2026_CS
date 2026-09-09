// 更换头像对话框：可选基础头像或本地图片，仅存本机
#include "ui/avatar_picker_dialog.h"

#include "ui/avatar_art.h"

#include <QButtonGroup>
#include <QFileDialog>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QImageReader>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

namespace charging::client {

// 构造对话框：设置标题、模态与选中态样式
AvatarPickerDialog::AvatarPickerDialog(const QImage &currentAvatar, QWidget *parent)
    : QDialog(parent)
{
    setObjectName(QStringLiteral("avatarPickerDialog"));
    setWindowTitle(QStringLiteral("更换头像"));
    setWindowModality(Qt::WindowModal);
    resize(320, 540);
    setStyleSheet(QStringLiteral(
        "QPushButton[avatarChoice] { padding: 8px; border-radius: 14px; }"
        "QPushButton[avatarChoice]:checked { background: #d8e9c3; border: 2px solid #527b64; }"
        "QPushButton[avatarChoice]:focus { border: 2px solid #245c45; }"));

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(20, 20, 20, 20);
    layout->setSpacing(12);
    auto *title = new QLabel(QStringLiteral("选一个喜欢的头像"), this);
    title->setProperty("role", "sectionTitle");
    layout->addWidget(title);
    // 预览区先显示当前头像，没有则显示占位图
    preview_ = new QLabel(this);
    preview_->setObjectName(QStringLiteral("avatarPreview"));
    preview_->setFixedSize(88, 88);
    preview_->setPixmap(circularAvatar(currentAvatar.isNull() ? defaultAvatar()
                                                            : currentAvatar,
                                       88, devicePixelRatioF()));
    layout->addWidget(preview_, 0, Qt::AlignHCenter);
    selectionLabel_ = new QLabel(QStringLiteral("选择基础头像，或使用本地图片"), this);
    selectionLabel_->setObjectName(QStringLiteral("avatarSelectionLabel"));
    selectionLabel_->setAlignment(Qt::AlignCenter);
    selectionLabel_->setWordWrap(true);
    layout->addWidget(selectionLabel_);

    // 生成四个基础头像按钮，点击即更新预览
    auto *grid = new QGridLayout();
    grid->setSpacing(10);
    avatarGroup_ = new QButtonGroup(this);
    const auto &avatars = basicAvatars();
    for (int i = 0; i < int(avatars.size()); ++i) {
        auto *button = new QPushButton(avatars[i].name, this);
        button->setObjectName(QStringLiteral("basicAvatar%1").arg(i));
        button->setProperty("avatarChoice", true);
        button->setCheckable(true);
        button->setAccessibleName(QStringLiteral("基础头像：%1").arg(avatars[i].name));
        button->setIcon(QIcon(circularAvatar(avatars[i].image, 44, devicePixelRatioF())));
        button->setIconSize(QSize(44, 44));
        button->setMinimumHeight(62);
        button->setAutoDefault(false);
        avatarGroup_->addButton(button, i);
        grid->addWidget(button, i / 2, i % 2);
        connect(button, &QPushButton::clicked, this, [this, i]() {
            const auto &avatar = basicAvatars()[i];
            selectImage(avatar.image, QStringLiteral("已选择：%1").arg(avatar.name));
        });
    }
    layout->addLayout(grid);

    // 本地图片按钮打开文件对话框选择图片
    auto *localButton = new QPushButton(QStringLiteral("从本地选择图片…"), this);
    localButton->setObjectName(QStringLiteral("localAvatarButton"));
    localButton->setAutoDefault(false);
    layout->addWidget(localButton);
    connect(localButton, &QPushButton::clicked, this, [this]() {
        auto *files = new QFileDialog(this, QStringLiteral("选择本地头像"));
        files->setObjectName(QStringLiteral("localAvatarFileDialog"));
        files->setAttribute(Qt::WA_DeleteOnClose);
        files->setFileMode(QFileDialog::ExistingFile);
        files->setNameFilter(QStringLiteral("图片文件 (*.png *.jpg *.jpeg *.bmp *.webp);;所有文件 (*)"));
        connect(files, &QFileDialog::fileSelected, this, &AvatarPickerDialog::selectLocalImage);
        files->open();
    });

    errorLabel_ = new QLabel(this);
    errorLabel_->setObjectName(QStringLiteral("avatarErrorLabel"));
    errorLabel_->setStyleSheet(QStringLiteral("color: #c62828;"));
    errorLabel_->setWordWrap(true);
    errorLabel_->hide();
    layout->addWidget(errorLabel_);
    auto *hint = new QLabel(QStringLiteral("头像仅保存在本机，不会上传。"), this);
    hint->setWordWrap(true);
    hint->setStyleSheet(QStringLiteral("color: #697969;"));
    layout->addWidget(hint);
    layout->addStretch();

    // 底部取消与确认按钮，未选择前确认不可用
    auto *actions = new QHBoxLayout();
    auto *cancel = new QPushButton(QStringLiteral("取消"), this);
    cancel->setObjectName(QStringLiteral("cancelAvatarButton"));
    cancel->setAutoDefault(false);
    confirmButton_ = new QPushButton(QStringLiteral("使用此头像"), this);
    confirmButton_->setObjectName(QStringLiteral("confirmAvatarButton"));
    confirmButton_->setProperty("role", "primary");
    confirmButton_->setEnabled(false);
    confirmButton_->setDefault(true);
    actions->addWidget(cancel);
    actions->addWidget(confirmButton_);
    layout->addLayout(actions);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(confirmButton_, &QPushButton::clicked, this, &QDialog::accept);
}

// 读取本地图片，失败则显示错误并保持原选择
void AvatarPickerDialog::selectLocalImage(const QString &path)
{
    if (path.isEmpty()) {
        return;
    }
    QImageReader reader(path);
    reader.setAutoTransform(true);
    const QImage image = reader.read();
    if (image.isNull()) {
        errorLabel_->setText(QStringLiteral("无法读取所选图片，请选择有效的图片文件。"));
        errorLabel_->show();
        return;
    }
    avatarGroup_->setExclusive(false);
    for (auto *button : avatarGroup_->buttons()) {
        button->setChecked(false);
    }
    avatarGroup_->setExclusive(true);
    selectImage(image, QStringLiteral("已选择：本地图片"));
}

// 选定后超过 512 像素等比缩小，更新预览并允许确认
void AvatarPickerDialog::selectImage(const QImage &image, const QString &description)
{
    selectedImage_ = image;
    if (image.width() > 512 || image.height() > 512) {
        selectedImage_ = image.scaled(512, 512, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }
    preview_->setPixmap(circularAvatar(selectedImage_, 88, devicePixelRatioF()));
    selectionLabel_->setText(description);
    errorLabel_->hide();
    confirmButton_->setEnabled(true);
}

}  // namespace charging::client
