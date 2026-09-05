# 服务端最小订单闭环

当前实现遵循 [V1 订单契约](../contracts/overall-interface-v1.md#63-订单主流程)和已有五表迁移，不增加数据库表、客户端消息别名或新订单状态。用户界面与 `TcpChargingApi` 可以直接复用。

## 已接入的流程

| 操作 | 订单变化 | 桩变化 | 余额 |
| --- | --- | --- | --- |
| 预约 | 新建 `RESERVED` | `IDLE → RESERVED` | 不变 |
| 取消 | `RESERVED → CANCELLED` | `RESERVED → IDLE` | 不变 |
| 预约开始 | 原单 `RESERVED → CHARGING` | `RESERVED → CHARGING` | 不变 |
| 直接开始 | 新建 `CHARGING` | `IDLE → CHARGING` | 不变 |
| 停止，余额够 | `CHARGING → COMPLETED` | `CHARGING → IDLE` | 扣完整金额 |
| 停止，余额不足 | `CHARGING → PENDING_PAYMENT` | `CHARGING → IDLE` | 不变，不部分扣款 |
| 充值后补付 | `PENDING_PAYMENT → COMPLETED` | 不变 | 扣冻结账单金额 |

`order.current` 返回本人的唯一当前单或 null；`order.list` 返回本人全部订单。预约、充电中、待支付都会阻止同一用户再开一单。每次调用重新校验 token 和用户冻结状态；跨用户操作被拒绝。

## 计费和事务

- `ApplicationService` 的订单逻辑集中在 `src/application/order_service.cpp`。`RepositoryTransaction` 负责在提前返回或提交失败时回滚；SQL 只出现在 Repository 中。
- 所有订单写路径使用同一 SQLite `BEGIN IMMEDIATE` 事务，涵盖检查、订单写入、占用/释放桩和余额更新。`updateOrder` 还检查预期旧状态；现有唯一索引保留最后一道一致性检查。
- `order.start` 冻结站点单价；预约阶段没有价格快照。开始后的站点调价不影响本单。
- `order_billing.h` 使用整数公式 `(energyWh * unitPriceCentsPerKwh + 500) / 1000`，并检查乘法溢出。`stop` 保存最终金额，`pay` 使用该金额，不重新计价。
- `IPileGateway` 只定义开始、读取、停止和重启；当前唯一运行实现是 `MockPile`。沿用 7.2 kW 固定演示曲线，每真实经过 1 秒增加 2 Wh，不按请求次数加电量。Mock 在同一充电过程中保持读数不倒退。
- 充电中的 DTO 在当前订单、历史列表、进度和管理员列表返回前读取 Mock；轮询不逐次写库。停止后将读数保存到订单，桩累计充电次数/时长随订单聚合更新。
- 进程重启后订单仍保留在 SQLite；token 在内存中，客户端需要重新登录。Mock 可根据订单开始时间恢复读数。
- 这里只承诺 Mock 的事务行为。未来真实设备的远程副作用不能由 SQLite 回滚，需要在设备扩展立项时设计失败确认/恢复协议。

## 本机联调

按 [database/README.md](../database/README.md) 在新数据库中执行迁移和种子，再从仓库根目录运行：

```bash
cmake -S server -B build/server -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build/server
ctest --test-dir build/server --output-on-failure
./build/server/server-app --database build/database/demo.db
```

在另一个终端使用已构建的客户端：

```bash
./build/client/user-client --api tcp --host 127.0.0.1 --port 45678 --map mock
```

服务端管理界面的交互见 [使用说明.md](使用说明.md)。普通表格单击选中、双击详情、右键操作；站点主行可展开，子桩双击跳转。查询与多选筛选可组合，顶部刷新重新读取当前页面；后退/前进只恢复视图，不撤销数据，详情窗口点击外部关闭。

推荐演示顺序：

1. 新库使用 `13800000001` 登录，查询 `PILE-A-01`；先预约、取消，再预约并开始。
2. 等待数秒并刷新进度，确认时长、电量增长；停止后确认扣款、订单完成、电桩回到空闲。
3. 使用一个新手机号（初始余额 0）直接开始，至少等待数秒后停止，确认待支付及电桩已释放。
4. 充值后补付，确认订单完成、余额只扣一次；重复停止/支付应返回 `40903`。
5. 管理端刷新订单、桩和营收页面，查看刚产生的业务结果；重启服务端后重新登录，历史订单仍可查。

固定 fixture 中的 ID、时间和电量只表示边界形状，运行时使用服务器返回的真实订单 ID、开始时间及读数。内存替身的演示种子与 SQLite 种子不同，跨后端测试使用新手机号，不依赖相同的预置订单。

## 验证范围

`charging_order_flow_tests` 覆盖两个 Repository 的正常/待支付闭环、归属、冻结、状态保护、价格快照、整数舍入、只读进度、账单不重复扣款，以及 SQLite 的插入/更新/提交失败回滚、重新打开数据库和真实客户端 TCP 适配器对接。

未来的预约到期与违约、复杂计价、故障报修、钱包流水/退款候选见 [订单扩展说明](../docs/extension/order-evolution.md)。这些不是本次 Demo 的前置条件。
