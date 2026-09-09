// 文件用途：主题样式与导航图标的对外接口
#pragma once

#include <QIcon>
#include <QString>

namespace charging::client {

// 界面中会用到的导航与功能图标枚举
enum class NavigationIcon {
    Charging,
    Orders,
    Scan,
    Support,
    Profile,
    Route,
    Location,
    Repair,
    Tickets,
    ChevronRight,
    Wallet,
};

// 分别提供全局、服务卡、钱包卡样式与图标获取
[[nodiscard]] QString clientThemeStyleSheet();
[[nodiscard]] QString profileServicesStyleSheet();
[[nodiscard]] QString profileWalletStyleSheet();
[[nodiscard]] QIcon clientNavigationIcon(NavigationIcon icon);

}  // namespace charging::client
