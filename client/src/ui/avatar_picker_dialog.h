#pragma once

#include <QDialog>
#include <QImage>

class QButtonGroup;
class QLabel;
class QPushButton;

namespace charging::client {

class AvatarPickerDialog final : public QDialog {
    Q_OBJECT

public:
    explicit AvatarPickerDialog(const QImage &currentAvatar, QWidget *parent = nullptr);
    const QImage &selectedImage() const { return selectedImage_; }
    void selectLocalImage(const QString &path);

private:
    void selectImage(const QImage &image, const QString &description);

    QImage selectedImage_;
    QLabel *preview_ = nullptr;
    QLabel *selectionLabel_ = nullptr;
    QLabel *errorLabel_ = nullptr;
    QPushButton *confirmButton_ = nullptr;
    QButtonGroup *avatarGroup_ = nullptr;
};

}  // namespace charging::client
