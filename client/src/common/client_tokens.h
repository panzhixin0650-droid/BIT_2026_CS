#pragma once

#include <QString>

namespace charging::client::tokens {

inline constexpr int Touch = 48;
inline constexpr int CriticalTouch = 56;
inline constexpr int Radius = 18;
inline constexpr int Space = 16;

[[nodiscard]] QString fontFamily();
[[nodiscard]] QString forest();
[[nodiscard]] QString warmWhite();
[[nodiscard]] QString surface();
[[nodiscard]] QString muted();

}  // namespace charging::client::tokens
