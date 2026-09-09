// 文件用途：结束充电前的二次确认弹窗
#include "ui/charging_stop_dialog.h"

#include <QAbstractButton>
#include <QMessageBox>

namespace charging::client {

// 提示会生成账单并尝试扣款，用户确认才返回真
bool confirmChargingStop(QWidget *parent)
{
    QMessageBox confirmation(parent);
    confirmation.setIcon(QMessageBox::Question);
    confirmation.setWindowTitle(QStringLiteral("确认结束充电"));
    confirmation.setText(
        QStringLiteral("结束充电后将生成最终账单，\n"
                       "并尝试从钱包自动扣款。\n"
                       "是否继续？"));
    confirmation.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
    confirmation.setDefaultButton(QMessageBox::No);
    confirmation.button(QMessageBox::Yes)->setText(QStringLiteral("结束充电"));
    confirmation.button(QMessageBox::No)->setText(QStringLiteral("取消"));
    return confirmation.exec() == QMessageBox::Yes;
}

}  // namespace charging::client
