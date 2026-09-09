#pragma once

#include "charging/protocol/dto.h"

#include <QWidget>
#include <QImage>

class QLabel;
class QLineEdit;
class QPushButton;
class QStackedWidget;

namespace charging::client {

// 个人中心页面声明：概览、详情、头像三段视图
class ProfilePage final : public QWidget {
    Q_OBJECT

public:
    explicit ProfilePage(QWidget *parent = nullptr);

    // 外部注入用户、余额、头像与忙碌状态
    void setUser(const protocol::UserDto &user);
    void setBalance(qint64 balanceCents);
    void setAvatarPath(const QString &path);
    void setBusy(bool busy);
    void showMessage(const QString &message, bool error = false);
    void openDetails();
    void showOverview();

// 向控制器上报的用户操作信号
signals:
    void ordersRequested();
    void repairRequested();
    void ticketsRequested();
    void avatarSelectionRequested();
    void refreshRequested();
    void nicknameUpdateRequested(const QString &nickname);
    void rechargeRequested(const QString &amountYuan);
    void avatarSelected(const QString &sourcePath);
    void avatarImageSelected(const QImage &image);
    void logoutRequested();

protected:
    void resizeEvent(QResizeEvent *event) override;
private:
    // 根据窗口尺寸刷新头像大图
    void updateFullAvatar();
    QStackedWidget *sections_ = nullptr;
    QLabel *detailMessage_ = nullptr;
    QLabel *avatarMessage_ = nullptr;
    QLabel *detailPhone_ = nullptr;
    QLabel *fullAvatar_ = nullptr;
    QPushButton *avatarButton_ = nullptr;
    QString savedNickname_;
    // 金额格式化：整数分转元显示
    [[nodiscard]] QString formatBalance(qint64 balanceCents) const;

    // 缓存头像图像及各控件指针，便于随时更新
    QImage avatarImage_;
    QLabel *avatarLabel_ = nullptr;
    QLabel *nicknameLabel_ = nullptr;
    QLabel *phoneLabel_ = nullptr;
    QLabel *balanceLabel_ = nullptr;
    QLabel *messageLabel_ = nullptr;
    QLineEdit *nicknameInput_ = nullptr;
    QLineEdit *rechargeInput_ = nullptr;
    QPushButton *refreshButton_ = nullptr;
    QPushButton *saveNicknameButton_ = nullptr;
    QPushButton *rechargeButton_ = nullptr;
    QPushButton *logoutButton_ = nullptr;
};

}  // namespace charging::client
