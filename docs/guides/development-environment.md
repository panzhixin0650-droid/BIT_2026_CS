# 开发环境基线与安装指南

## 1. 结论

本项目统一记录的参考开发机是 **Ubuntu 22.04.3 LTS x86_64 + Linux 6.8.0-138 + Qt Framework 6.2.4 + Qt Creator 6.0.2 + GCC 11.4.0 + C++17**。

这是最终构建和演示所用的参考环境，不是要求所有成员日常使用完全相同的编辑器。代码兼容门槛是 Qt 6.2.4、GCC 11 和 C++17；内核号同时作为参考机快照记录，不要求成员为了普通安全补丁降级内核。使用不同环境的成员应在上述参考机上完成合并前验证。

当前仓库仍只有契约、设计和目录骨架，没有 CMake 工程或应用源码。因此本页定义的是后续实现必须遵守的环境，不表示客户端或服务端现在已经可以构建。

## 2. Qt Creator 6.0.2 和 Qt 6.2.4 没有冲突

团队确认的实际环境是 Qt Creator 6.0.2 和 Qt Framework 6.2.4。Qt Creator 是写代码和启动构建的 IDE，Qt Framework 才是应用编译、链接和运行所用的库，因此两者版本号不需要相同。Ubuntu 22.04 软件源恰好提供这两个版本组合。

项目说明书中的“Qt Creator 6.2 及以上”存在命名歧义：Qt Creator 的正式版本从 6.0 系列直接进入 7.0 系列，并不存在 6.2 正式版。当前项目按团队给出的实机环境，把其中的 6.2 解释为 Qt Framework 版本，并明确记录为：

- 应用的编译兼容基线是 **Qt Framework 6.2.4**；
- 统一使用 Ubuntu 软件源的 **Qt Creator 6.0.2**；
- 判断项目实际使用的 Qt 版本，应查看 Kit 或执行 `qmake6 -query QT_VERSION`，不能看“Qt Creator 基于哪个 Qt 运行”。

Qt Creator 6.0.2 自身可能显示“Based on Qt 5.15”，这只描述 IDE 自身，不表示项目使用 Qt 5。只要 Kit 指向 `/usr/bin/qmake6`，项目仍由 Qt 6.2.4 构建。若验收人员按说明书字面核查 IDE 版本，应提前说明这一版本事实并确认口径，不能临近答辩才发现歧义。

## 3. 统一版本表

Ubuntu、内核、Qt Framework、Qt Creator 和 GCC 是团队确认的参考机版本；VMware 17 来自项目说明书；C++17、CMake、Ninja、SQLite 和文本格式是仓库为保证多人协作作出的工程约定。

| 项目 | 团队参考/验收基线 | 说明 |
| --- | --- | --- |
| 操作系统 | Ubuntu 22.04.3 LTS x86_64 | 最终演示和合并验证以此为准；Qt GUI 推荐 Desktop 安装 |
| Linux 内核 | 6.8.0-138 | `uname -r` 可能显示发行版后缀，例如 `-generic` |
| 虚拟机 | VMware Workstation 17 | 仅 Windows 主机需要；原生 Ubuntu 不需要 |
| Qt Framework | 6.2.4 | Ubuntu 22.04 官方仓库版本；只使用 Qt 6.2 API |
| Qt Creator | 6.0.2 | Ubuntu 22.04 官方仓库版本；Kit 使用 Qt 6.2.4 |
| C++ 标准 | C++17 | Qt 6 本身要求 C++17 或更新版本 |
| 编译器 | GCC/G++ 11.4.0 | 团队确认的参考环境 |
| 构建系统 | CMake 3.22.x | 各 C++ 模块统一使用 CMake，不再维护 qmake 工程 |
| 构建工具 | Ninja 1.10.x | 所有人使用同一种默认生成器 |
| 调试器 | GDB 12.x | Ubuntu 22.04 软件源版本即可 |
| 数据库 | SQLite 3.37.x + Qt QSQLITE | 不安装 MySQL、PostgreSQL 或数据库服务 |
| Web | 静态 HTML/CSS/JS + ECharts + 浏览器 | 当前不需要 Node.js/npm |
| 文本格式 | UTF-8、LF | 由根目录 `.editorconfig` 约束 |

软件包版本可能带 Ubuntu 发行版后缀，这是正常的。验收记录应保留上表中的核心版本；内核安全更新或 Ubuntu 包修订号变化只需记录差异，不等于源代码不兼容。

## 4. 按角色安装

### 4.1 所有成员

```bash
sudo apt update
sudo apt install -y \
  git openssh-client ca-certificates
```

GitHub 账号、SSH 和分支协作配置见[Ubuntu 与 GitHub 协作教程](github-collaboration.md)。

Web 负责人另外安装用于本地静态服务的 Python：

```bash
sudo apt install -y python3
```

### 4.2 Qt 客户端、服务端和共享协议开发者

Ubuntu 22.04 的 Qt 6 包位于 `universe`。如果系统尚未启用该仓库，先执行：

```bash
sudo apt install -y software-properties-common
sudo add-apt-repository -y universe
sudo apt update
```

再安装公共编译环境：

```bash
sudo apt install -y \
  build-essential cmake ninja-build gdb pkg-config \
  qt6-base-dev libqt6sql6-sqlite \
  qtcreator designer-qt6 \
  sqlite3
```

其中：

- `qt6-base-dev` 已提供 Core、Gui、Widgets、Network、Sql、Test，并通过依赖带入由 CMake 调用的 `moc`、`uic`、`rcc`；
- `libqt6sql6-sqlite` 提供运行时 QSQLITE 驱动，必须显式安装；
- `qtcreator` 安装团队统一的 Qt Creator 6.0.2，`designer-qt6` 用于编辑 `.ui`；
- `sqlite3` 只用于查看数据库和验证迁移；应用通过 Qt Sql 访问数据库，不需要 `libsqlite3-dev`；
- Socket 使用 Qt Network 的 `QTcpServer`/`QTcpSocket`，不需要安装不存在的 `libsocket-dev`；
- `QThread` 和信号槽来自 Qt Core，pthread 接口随系统 C/C++ 工具链提供，不需要额外安装 `libpthread-dev`。当前仍按 ADR-0001 允许串行运行，只保留可选 QThread 架构边界。

### 4.3 按功能增加的包

| 谁需要 | 安装命令 | 何时需要 |
| --- | --- | --- |
| 服务端/管理端负责人 | `sudo apt install -y libqt6charts6-dev` | 实现说明书的 QChart 营收折线前必装；不是全员必装 |
| 客户端负责人 | `sudo apt install -y qt6-webengine-dev` | Mock 地图阶段可延后；若最终演示内嵌腾讯导航则必装 |
| 使用虚拟机的成员 | `sudo apt install -y open-vm-tools-desktop` | 改善 VMware 分辨率、剪贴板和鼠标体验 |
| 中文字体缺失的成员 | `sudo apt install -y fonts-noto-cjk` | Qt 或浏览器不能正确显示中文时 |

当前地图默认使用 Mock，`qt6-webengine-dev` 体积较大，不是全员前置依赖。Web 页面读取静态 JSON，不需要安装 Qt，也不需要 Node.js/npm。

### 4.4 当前不要安装为项目依赖

- Docker、Redis、消息队列、MySQL 或 PostgreSQL；
- CUDA、Python 机器学习环境或真实预测模型；
- 真实充电桩 SDK；
- `libsodium-dev` 或其他扩展文档中的工业级安全依赖；
- `libsqlite3-dev`，除非以后明确决定绕过 Qt Sql 直接调用 SQLite C API；
- 腾讯地图 Key，Mock 地图开发不依赖它。

这些工具不是被永久禁止，而是尚未成为当前 Demo 的开发前置条件。启用扩展功能时应先按 ADR-0001 的流程确认。

## 5. 配置 Qt Creator Kit

Qt 客户端和服务端开发者按以下步骤配置：

1. 启动通过 Ubuntu 软件源安装的 Qt Creator 6.0.2；
2. 在 `Tools → Options → Kits` 中添加或检查 Qt version，指向 `/usr/bin/qmake6`；
3. 建立名为 `Desktop Qt 6.2.4 GCC 64bit` 的 Desktop Kit；
4. Compiler 选择 `/usr/bin/g++`，Debugger 选择 `/usr/bin/gdb`；
5. CMake 选择 `/usr/bin/cmake`，Generator 选择 Ninja。

项目只使用 CMake。`qmake6` 在这里仅帮助 Qt Creator 识别系统 Qt，不代表项目还要维护 `.pro` 文件。不要在同一个构建目录里混用 Ubuntu 的 Qt 与官方安装器下载的另一套 Qt；切换 Kit 后应使用新的 `build/` 子目录。

## 6. 各模块的依赖边界

| 模块 | 当前 Qt CMake components | 可以不安装什么 |
| --- | --- | --- |
| `shared/protocol/` | Core | Widgets、Sql、Charts、WebEngine |
| `client/` | Core、Gui、Widgets、Network | Sql、Charts；Mock 地图阶段不需要 WebEngineWidgets |
| `server/` | Core、Gui、Widgets、Network、Sql | WebEngineWidgets；实现管理图表时再增加 Charts |
| Qt 测试 | Test | 只在对应模块写 Qt Test 时链接 |
| `database/` | 无独立 Qt target | 迁移编写只需 `sqlite3`；Repository 属于服务端 |
| `web/` | 无 Qt module | Qt Creator、C++ 编译器和 Node.js/npm |

模块负责人只能把自己确实使用的 component 写入本模块的 CMake 配置，不能因为某个扩展文档提到过功能，就让所有模块共同依赖它。

## 7. 环境自检

Qt/C++ 开发者安装完成后执行：

```bash
. /etc/os-release
printf 'OS: %s\n' "$PRETTY_NAME"
uname -r
uname -m

git --version
g++ --version | head -n 1
cmake --version | head -n 1
ninja --version
qmake6 -query QT_VERSION
sqlite3 --version

dpkg-query -W -f='${Package}\t${Version}\n' \
  qtcreator qt6-base-dev libqt6sql6-sqlite

qt_plugin_dir="$(qmake6 -query QT_INSTALL_PLUGINS)"
test -f "$qt_plugin_dir/sqldrivers/libqsqlite.so" \
  && echo 'QSQLITE plugin: OK' \
  || echo 'QSQLITE plugin: MISSING'
```

关键结果应为：

- 参考环境的 OS 是 Ubuntu 22.04.3 LTS，`uname -r` 以 `6.8.0-138` 开头，架构是 `x86_64`；
- G++ 是 11.4.0，CMake 是 3.22.x；
- `qmake6` 返回 6.2.4；
- Qt Creator 返回 6.0.2；
- QSQLITE plugin 显示 `OK`。

Qt Creator 的版本由上面的 `dpkg-query` 查看，也可在 `Help → About Qt Creator` 中确认。第一次涉及代码的 Pull Request 应在描述中贴出 OS、内核、Qt Framework、Qt Creator、G++ 和 CMake 结果；无需提交一份会随机器变化的环境报告文件。

## 8. 允许的个人差异

成员可以在其他环境或编辑器中临时编辑，但必须满足：

1. 不使用 Qt 6.2.4 或 GCC 11 无法编译的 API/语言特性；
2. 不提交 IDE 用户配置、绝对路径或编译产物；
3. 不让另一个包管理器成为其他成员构建的隐藏条件；
4. 合并前由自己或队友在统一基线上验证；
5. 最终课程演示在已记录的 Ubuntu 22.04.3、内核 6.8.0-138、Qt 6.2.4、Qt Creator 6.0.2、GCC 11.4.0 参考机上重新验证；若内核只因安全更新发生变化，在验收记录中注明即可。

如果某项功能只能在更新环境运行，应先通过契约或 ADR 明确提高基线，不能由某个模块悄悄升级。

## 9. 虚拟机和运行注意事项

- 使用有图形会话的 Ubuntu，不用无桌面的 Ubuntu Server 作为 Qt GUI 主开发环境；
- 若使用 Windows 主机且老师未明确豁免说明书中的 VMware 17，最终版本至少在 VMware Workstation 17 中完成一次启动和主流程冒烟；
- 普通 Demo 建议给虚拟机 4 个虚拟 CPU、8 GB 内存和至少 40 GB 可用磁盘；这是体验建议，不是接口契约；
- 仓库、`build/` 和运行时 SQLite 文件放在虚拟机本地 Linux 文件系统，不直接放进 VMware 共享目录；
- 客户端与服务端在同一台系统运行时使用 `127.0.0.1`，不要为了联调直接关闭防火墙；
- 不用 `sudo` 或 root 运行 Qt GUI、服务端或 Qt WebEngine；
- 保持系统时间同步。协议时间使用 UTC ISO 8601，营收自然日仍按 `Asia/Shanghai`，不依赖开发机当前时区；
- 虚拟机快照用于恢复系统，Git 用于协作和版本历史，两者不能互相替代。

## 10. 实现开始后的独立构建目标

第一批实现各模块构建入口的 Pull Request，应最终让下面三组命令互不依赖地成立：

```bash
cmake -S shared/protocol -B build/protocol -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build/protocol
ctest --test-dir build/protocol --output-on-failure
```

```bash
cmake -S client -B build/client -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build/client
ctest --test-dir build/client --output-on-failure
```

```bash
cmake -S server -B build/server -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build/server
ctest --test-dir build/server --output-on-failure
```

这些是未来构建契约，不表示当前仓库已有对应 `CMakeLists.txt`。根目录聚合构建可以以后增加，但不能成为客户端或服务端独立开发的前置条件。

Web 开始实现后通过 HTTP 访问，避免浏览器因 `file://` 安全限制而无法读取 JSON：

在仓库根目录执行：

```bash
python3 -m http.server 8000 --bind 127.0.0.1 --directory web
```

## 11. 官方资料

- [Qt 6 with CMake：Qt 6 要求 C++17 或更新版本](https://doc.qt.io/qt-6/cmake-get-started.html)
- [Qt Creator 安装说明](https://doc.qt.io/qtcreator/creator-how-to-install.html)
- [Qt Creator Kit 配置](https://doc.qt.io/qtcreator/creator-configuring.html)
- [Qt Creator 官方历史版本目录](https://download.qt.io/archive/qtcreator/)
- [Ubuntu 22.04 的 Qt Creator 6.0.2 包](https://packages.ubuntu.com/jammy/amd64/qtcreator)
- [Ubuntu 22.04 的 Qt 6.2.4 开发包](https://packages.ubuntu.com/jammy/qt6-base-dev)
- [Ubuntu 22.04 的 QSQLITE 驱动](https://packages.ubuntu.com/jammy/libqt6sql6-sqlite)
- [Ubuntu 22.04 的 Qt Charts 开发包](https://packages.ubuntu.com/jammy/libqt6charts6-dev)
- [Ubuntu 22.04 的 Qt WebEngine 开发包](https://packages.ubuntu.com/jammy/qt6-webengine-dev)
