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
};

[[nodiscard]] QString clientThemeStyleSheet();
[[nodiscard]] QIcon clientNavigationIcon(NavigationIcon icon);

}  // namespace charging::client
