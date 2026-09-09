// 头像选择对话框声明
#pragma once

#include <QDialog>
#include <QImage>

class QButtonGroup;
class QLabel;
class QPushButton;

namespace charging::client {

// 头像选择对话框，结果通过 selectedImage 取回
class AvatarPickerDialog final : public QDialog {
    Q_OBJECT

public:
    // 传入当前头像用于初始预览
    explicit AvatarPickerDialog(const QImage &currentAvatar, QWidget *parent = nullptr);
    const QImage &selectedImage() const { return selectedImage_; }
    // 公开选择本地文件的入口，便于测试直接调用
    void selectLocalImage(const QString &path);

private:
    // 统一处理缩放、预览与提示文字
    void selectImage(const QImage &image, const QString &description);

    // 保存最终选中的头像图像
    QImage selectedImage_;
    QLabel *preview_ = nullptr;
    QLabel *selectionLabel_ = nullptr;
    QLabel *errorLabel_ = nullptr;
    QPushButton *confirmButton_ = nullptr;
    QButtonGroup *avatarGroup_ = nullptr;
};

}  // namespace charging::client
