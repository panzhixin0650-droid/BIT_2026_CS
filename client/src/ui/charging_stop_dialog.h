// 文件用途：声明结束充电确认弹窗接口
#pragma once

class QWidget;

namespace charging::client {

// 返回值表示用户是否确认结束充电
[[nodiscard]] bool confirmChargingStop(QWidget *parent);

}  // namespace charging::client
