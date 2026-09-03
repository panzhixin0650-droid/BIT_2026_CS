# Qt 服务端与管理员端

本目录由服务端负责人维护，目标程序为同一个 `server-app`：TCP 服务、业务服务和管理员 QWidget 共进程。

## 服务端文档

- [`版本日志.md`](版本日志.md)：只记录服务端与同进程管理员端的版本，后续版本继续在此文件中追加；
- [`服务端网络接口.md`](服务端网络接口.md)：记录服务端当前已经完成、可供客户端联调的 TCP 接口。

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
- `RequestRouter` / `ApplicationService`：已打通 ping、用户登录/退出、资料、充值和站点查询；
- `Repository`：建立 QSQLITE 唯一访问边界并在连接后开启外键，尚未加入业务查询；
- `InMemoryRepository`：数据库迁移合并前的开发替身，提供演示管理员、用户、站点、电桩和订单，进程退出后修改丢失；
- 管理员 UI：支持登录、7/30 日概览、站点新增、电桩模拟重启、用户查询及冻结/解冻、订单列表；
- `AdminFacade`：所有管理员界面操作均通过 `ApplicationService`，UI 不直接访问 Repository；
- `charging_server_tests`：覆盖基础 TCP 业务、管理员登录、Dashboard、站点新增、桩重启和冻结限制。

当前不创建数据库、不执行迁移。SQLite Repository 接入后将替换内存实现，不改变 UI、TCP 和 ApplicationService 对外语义。

当前内存模式支持的 V1 TCP 消息：

- `system.ping`；
- `auth.user.login`、`auth.logout`；
- `user.profile.get`、`user.profile.update`、`wallet.recharge`；
- `station.list`、`station.detail`。

可用 `13800000001` 登录；`13800000005` 是冻结用户失败场景。内存数据仅用于数据库就绪前的服务端开发与客户端联调，不是数据库迁移或种子的替代品。

## 构建和运行

```bash
cmake -S server -B build/server -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build/server
ctest --test-dir build/server --output-on-failure

./build/server/server-app
```

默认只绑定本机地址，端口为 `45678`。开发时可传 `--port <端口>`；只查看管理员窗口时可传 `--no-tcp`。

管理员演示账号是 `admin / 123456`。在 Qt Creator 中先运行 CMake，然后将运行目标从
`charging_protocol_tests` 切换为 `server-app`，点击左下角绿色运行按钮即可看到登录页。
如果使用仓库内已经配置的 Desktop Debug 构建目录，也可以在终端直接运行：

```bash
./build-server-Desktop-Debug/server-app --no-tcp
```
