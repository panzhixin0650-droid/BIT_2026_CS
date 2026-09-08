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
    Location,
    Repair,
    Tickets,
    ChevronRight,
    Wallet,
};

[[nodiscard]] QString clientThemeStyleSheet();
[[nodiscard]] QString profileServicesStyleSheet();
[[nodiscard]] QString profileWalletStyleSheet();
[[nodiscard]] QIcon clientNavigationIcon(NavigationIcon icon);

}  // namespace charging::client
