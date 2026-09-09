// 本文件声明启动前调整输入法环境的辅助函数
#pragma once

#include <QString>

namespace charging::client {

// Must be called before QApplication is constructed. Returns true only when
// this process changes QT_IM_MODULE from an unavailable Fcitx Qt plugin to the
// available IBus compatibility plugin.
[[nodiscard]] bool configureInputMethodForQt(const QString &qtPluginsPath = {});

}  // namespace charging::client
