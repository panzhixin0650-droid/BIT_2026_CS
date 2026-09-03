#include "ui/main_window.h"

#include "api/i_charging_api.h"
#include "ui/login_controller.h"
#include "ui/login_page.h"

#include <QLabel>
#include <QStackedWidget>
#include <QVBoxLayout>

namespace charging::client {

MainWindow::MainWindow(IChargingApi &api, QWidget *parent)
    : QMainWindow(parent)
{
    setObjectName(QStringLiteral("mainWindow"));
    setWindowTitle(QStringLiteral("新能源汽车充电服务"));
    resize(420, 760);
    setMinimumSize(360, 640);

    pages_ = new QStackedWidget(this);
    pages_->setObjectName(QStringLiteral("applicationPages"));
    loginPage_ = new LoginPage(pages_);

    homePage_ = new QWidget(pages_);
    homePage_->setObjectName(QStringLiteral("authenticatedHomePage"));
    auto *homeLayout = new QVBoxLayout(homePage_);
    homeLayout->setContentsMargins(28, 36, 28, 36);
    homeLayout->setSpacing(12);

    welcomeLabel_ = new QLabel(homePage_);
    welcomeLabel_->setObjectName(QStringLiteral("welcomeLabel"));
    QFont welcomeFont = welcomeLabel_->font();
    welcomeFont.setPointSize(18);
    welcomeFont.setBold(true);
    welcomeLabel_->setFont(welcomeFont);

    loginNoticeLabel_ = new QLabel(homePage_);
    loginNoticeLabel_->setObjectName(QStringLiteral("loginNoticeLabel"));
    loginNoticeLabel_->setStyleSheet(QStringLiteral("color: #1677ff;"));

    auto *placeholder = new QLabel(QStringLiteral("登录成功，充电首页将在下一阶段接入"),
                                   homePage_);
    placeholder->setObjectName(QStringLiteral("appPlaceholder"));
    placeholder->setAlignment(Qt::AlignCenter);
    placeholder->setWordWrap(true);

    homeLayout->addWidget(welcomeLabel_);
    homeLayout->addWidget(loginNoticeLabel_);
    homeLayout->addStretch();
    homeLayout->addWidget(placeholder);
    homeLayout->addStretch();

    pages_->addWidget(loginPage_);
    pages_->addWidget(homePage_);
    pages_->setCurrentWidget(loginPage_);
    setCentralWidget(pages_);

    loginController_ = new LoginController(*loginPage_, api, this);
    connect(loginController_,
            &LoginController::loginSucceeded,
            this,
            &MainWindow::showAuthenticatedHome);
}

void MainWindow::showAuthenticatedHome(const protocol::UserDto &user, bool isNewUser)
{
    welcomeLabel_->setText(QStringLiteral("你好，%1").arg(user.nickname));
    loginNoticeLabel_->setText(isNewUser ? QStringLiteral("账号已自动注册并登录")
                                         : QStringLiteral("登录成功"));
    pages_->setCurrentWidget(homePage_);
}

}  // namespace charging::client
