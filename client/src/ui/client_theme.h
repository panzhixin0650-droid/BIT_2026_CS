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
};

[[nodiscard]] QString clientThemeStyleSheet();
[[nodiscard]] QIcon clientNavigationIcon(NavigationIcon icon);

}  // namespace charging::client
