#pragma once

#include "charging/protocol/dto.h"

#include <QWidget>

class QLabel;
class QLineEdit;
class QPushButton;
class QStackedWidget;

namespace charging::client {

class ProfilePage final : public QWidget {
    Q_OBJECT

public:
    explicit ProfilePage(QWidget *parent = nullptr);

    void setUser(const protocol::UserDto &user);
    void setBalance(qint64 balanceCents);
    void setAvatarPath(const QString &path);
    void setBusy(bool busy);
    void showMessage(const QString &message, bool error = false);
    void openDetails();
    void showOverview();

signals:
    void ordersRequested();
    void repairRequested();
    void ticketsRequested();
    void refreshRequested();
    void nicknameUpdateRequested(const QString &nickname);
    void rechargeRequested(const QString &amountYuan);
    void avatarSelectionRequested();
    void avatarSelected(const QString &sourcePath);
    void logoutRequested();

protected:
    void resizeEvent(QResizeEvent *event) override;
private:
    void updateFullAvatar();
    QStackedWidget *sections_ = nullptr;
    QLabel *detailMessage_ = nullptr;
    QLabel *avatarMessage_ = nullptr;
    QLabel *detailPhone_ = nullptr;
    QLabel *fullAvatar_ = nullptr;
    QPushButton *avatarButton_ = nullptr;
    QString avatarPath_;
    QString savedNickname_;
    [[nodiscard]] QString formatBalance(qint64 balanceCents) const;

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
