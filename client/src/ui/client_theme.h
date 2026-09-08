#pragma once

#include <QIcon>
#include <QString>

namespace charging::client {

enum class NavigationIcon {
    Charging,
    Orders,
    Scan,
    Support,
    Profile,
    Route,
    Repair,
    Tickets,
    ChevronRight,
    Wallet,
};

[[nodiscard]] QString clientThemeStyleSheet();
[[nodiscard]] QString profileThemeStyleSheet();
[[nodiscard]] QIcon clientNavigationIcon(NavigationIcon icon);

}  // namespace charging::client
