# Qt 服务端与管理员端

本目录由服务端负责人维护，目标程序为同一个 `server-app`：TCP 服务、业务服务和管理员 QWidget 共进程。

## 服务端文档

- [`版本日志.md`](版本日志.md)：只记录服务端与同进程管理员端的版本，后续版本继续在此文件中追加；
- [`服务端网络接口.md`](服务端网络接口.md)：记录服务端当前已经完成、可供客户端联调的 TCP 接口。
- [`uidesign.md`](uidesign.md)：管理员端全局视觉与交互规范；
- [`logic.md`](logic.md)：当前业务规则和管理员端交互逻辑；
- [`improvement.md`](improvement.md)：已确认改进项及实现记录。
- [`使用说明.md`](使用说明.md)：管理员端页面、单击/双击/右键、导航、搜索筛选和业务操作说明；启动服务端前 Agent 必须阅读。
- [`order-flow.md`](order-flow.md)：当前订单闭环、事务边界、客户端联调步骤和测试。

## 边界

- TCP Gateway 只做连接、分帧、鉴权上下文和路由；
- `ApplicationService` 负责校验、订单状态、计费和事务编排；
- Repository 是唯一 SQL 入口；
- 管理员 UI 通过 `AdminFacade` 使用同一业务服务；
- 当前只有 `MockPile` 和 `MockPredictionProvider`，不实现真实设备协议；
- 默认串行运行；可选 QThread 只作为课程结构展示边界。

推荐逐步建立：

```text
src/transport/       TcpGateway、FrameCodec、RequestRouter
src/application/     ApplicationService、DTO 映射、业务规则
src/persistence/     Repository、Qt QSQLITE
src/admin_ui/        QWidget、AdminFacade
src/adapters/        MockPile、预测 Mock、DashboardExporter
forms/               Qt Designer 文件
resources/           管理端资源
tests/               服务端单元测试
```

## 当前实现

当前服务端已经提供可独立构建和运行的 Qt 6/C++17 Demo：

- `server-app`：启动管理员登录页和管理后台，并默认在 `127.0.0.1:45678` 监听 TCP；
- `TcpGateway`：复用 `shared/protocol` 处理长度帧和 JSON，请求按顺序交给路由；
- `RequestRouter` / `ApplicationService`：已打通 ping、用户登录/退出、资料、充值、站点查询及 8 个订单接口；
- `Repository`：通过 QSQLITE 实现用户、管理员、站点、电桩和订单的数据访问，以及订单事务读写；连接后校验外键、结构版本与五张 Demo 表；
- `InMemoryRepository`：保留为显式启用的开发和单元测试替身，进程退出后修改丢失；
- 管理员 UI：支持登录、7/30 日及自定义日期概览、站点搜索/筛选/新增/安全删除和站内电桩展开（新增站点时可编辑初始电桩编号、类型和功率）、电桩搜索/筛选/新增/安全删除/上线/下线/重启/故障标记、用户查询及冻结/解冻、订单查看，以及站点/电桩/用户/订单详情；
- `AdminFacade`：所有管理员界面操作均通过 `ApplicationService`，UI 不直接访问 Repository；
- `charging_server_tests`：覆盖基础 TCP 业务、管理员登录、Dashboard、自定义日期、站点新增/安全删除、电桩生命周期和冻结限制；
- `charging_admin_ui_tests`：覆盖登录失败后的输入保留、焦点与布局稳定、重试成功，以及焦点切换、错误提示和窗口缩放后的实际屏幕像素；
- `charging_repository_tests`：在临时 SQLite 数据库中覆盖种子读取、派生聚合、持久化、结构拒绝、站点/电桩安全删除和事务回滚。
- `charging_order_flow_tests`：对 SQLite 和内存替身验证预约、取消、直接/预约充电、实时读数、自动结算、充值补付、归属/状态校验和失败回滚，并用客户端 `TcpChargingApi` 对接真实 `TcpGateway`。

`server-app` 默认使用 SQLite，但不在运行时创建数据库或执行迁移。启动前按
[`database/README.md`](../database/README.md) 初始化数据库；服务端会拒绝不存在的文件、未知 `user_version` 或缺少 Demo 表的数据库。该切换不改变 UI、TCP 和 ApplicationService 对外语义。
SQLite 作为 `server-app` 内的嵌入式数据库直接读写本地文件，不监听端口，也不经过 TCP；TCP 只用于 Qt 用户端与服务端的通信。

当前支持的 V1 TCP 消息：

- `system.ping`；
- `auth.user.login`、`auth.logout`；
- `user.profile.get`、`user.profile.update`、`wallet.recharge`；
- `station.list`、`station.detail`；
- `order.current`、`order.reserve`、`order.cancel`、`order.start`；
- `order.progress`、`order.stop`、`order.pay`、`order.list`。

订单写操作由 `ApplicationService` 在同一事务中编排，SQLite 使用 `BEGIN IMMEDIATE`，任何中途失败均回滚订单、桩状态和余额。进度只在返回时读取 Mock，不逐次写库；开始时冻结站点单价，停止后冻结最终账单。重复停止/补支付返回 `40903`，不会再次扣款。

`prediction.latest` 仍未接入。当前订单使用现有五表和 V1，不启用预约到期、违约、分时计费、退款或报修状态；未来接入位置见[订单扩展候选说明](../docs/extension/order-evolution.md)。

数据库演示数据可用 `13800000001` 登录；`13800000004` 是冻结用户失败场景，`13800000005` 是待支付场景。编号迁移和种子始终是数据库事实源。

## 构建和运行

```bash
cmake -S server -B build/server -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build/server
ctest --test-dir build/server --output-on-failure

./build/server/server-app --database build/database/demo.db
```

数据库参数默认值就是 `build/database/demo.db`，从仓库根目录启动时可以省略。默认只绑定本机地址，端口为 `45678`。开发时可传 `--port <端口>`；只查看管理员窗口时可传 `--no-tcp`。只有测试或数据库尚未初始化时才显式传 `--in-memory` 使用非持久化替身。

管理员演示账号是 `admin / 123456`。在 Qt Creator 中先运行 CMake，然后将运行目标从
`charging_protocol_tests` 切换为 `server-app`，点击左下角绿色运行按钮即可看到登录页。
如果使用仓库内已经配置的 Desktop Debug 构建目录，也可以在终端直接运行：

```bash
./build-server-Desktop-Debug/server-app \
  --database /path/to/BIT_2026_CS/build/database/demo.db \
  --no-tcp
```

### 登录页重绘回归检查

`charging_admin_ui_tests` 使用内存测试替身，不读写运行时数据库，也不启动 TCP。
CTest 默认通过 `offscreen` 平台运行。像素检查读取 `QScreen::grabWindow()` 返回的已呈现画面，
避免 `QWidget::grab()` 触发完整重绘而掩盖局部重绘问题；同时检查密码标签、错误提示及其上下空隙。
若当前平台不支持屏幕抓取，像素用例会明确标记为跳过，登录行为检查仍会执行。

在已安装 Xvfb 的 Linux 环境中，也可检查 X11 和显示缩放：

```bash
xvfb-run -a -s "-screen 0 3840x2400x24" \
  env QT_QPA_PLATFORM=xcb QT_SCALE_FACTOR=1.5 \
  ./build/server/charging_admin_ui_tests
```

可将缩放比例改为 `1`、`1.25`、`1.75`、`2`。如需保存截图，在运行前将
`CHARGING_UI_ARTIFACT_DIR` 设为已创建的可写目录；截图属于本地验证产物，不提交到仓库。
该问题需特别在项目基线 Qt 6.2.4 中验证，不能仅以较新 Qt 下未复现作为通过依据。

## 管理员端快速使用

完整操作方式见 [`使用说明.md`](使用说明.md)。管理员端的基本交互约定如下：普通表格单击选中、双击打开居中详情、右键打开当前行操作；充电站主行单击展开/收起，展开后的电桩双击跳转到充电桩管理并高亮。搜索框按输入即时匹配，筛选通过独立的多选弹窗选择后确认，搜索和筛选可以组合使用，重置清空当前页面的全部条件。顶部后退、刷新、前进只恢复页面视图（包括筛选、展开和定位状态），不撤销新增、编辑、删除或设备状态操作；详情窗口点击窗口外关闭，窗口内部点击不会关闭。

### Agent 启动服务端的必读与告知要求

当用户要求启动服务端或 `server-app` 时，所有 Agent 必须先阅读 [`使用说明.md`](使用说明.md)，再执行启动或验收。Agent 的回复必须明确告知用户该说明书的位置，并简要复述单击/双击/右键、充电站展开与电桩跳转、搜索与多选筛选、后退/刷新/前进以及详情窗口的主要用法。启动失败时也必须先完成这项告知，再报告错误和排查结果。后退/前进是页面浏览历史，不是数据库数据撤销；Agent 不得据此向用户承诺数据回滚或说明书中未列出的功能。
