# Qt 服务端与管理员端

本目录由服务端负责人维护，目标程序为同一个 `server-app`：TCP 服务、业务服务和管理员 QWidget 共进程。

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

本次只建立责任边界，不提前创建业务类、数据库代码或 CMake target；服务端负责人开始实现时自行补齐。
