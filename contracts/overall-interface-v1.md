# 东软电动汽车充电桩应用管理平台：课程 Demo 整体接口契约

> 版本：V1.0（精简基线）
>
> 适用：Qt 用户端、Qt 服务端/管理端、SQLite、Web ECharts 页面、Mock 预测
>
> 原则：先保证模块能独立开发和主流程能联调；不预设工业级并发、可靠性或真实硬件实现。
>
> 配套文件：[数据库设计](../docs/design/demo-database-design.md)、[架构图 SVG](../docs/design/architecture/demo-architecture.svg)、[架构图 PNG](../docs/design/architecture/demo-architecture.png)

---

## 1. 本契约的地位与范围

### 1.1 唯一事实源

从本版开始：

- 跨进程消息名、字段名、字段类型和错误码以本文为准；
- 当前数据表和字段以《课程 Demo 数据库设计》为准；实现开始后，再由已经合并的编号迁移 SQL 成为结构事实源；
- 旧版 C/S 契约、数据库方案、接口总纲和 A1 契约只作参考，不再约束实现；
- 类名、文件夹、线程数量和 SQL 语句写法由实现者自行决定，只要不破坏本文边界。

这样可以避免旧文档之间互相引用、互相冲突。

### 1.2 当前交付模块

项目按说明书保留 4 个交付模块和 1 个演示扩展：

| 模块 | Demo 目标边界 |
| --- | --- |
| Qt 用户端 | 页面、客户端控制层、`IChargingApi`；可切 Mock 或 TCP |
| Qt 服务端/管理端 | TCP Gateway、业务服务、Repository、管理员 UI |
| SQLite 数据库 | 5 张核心表 |
| Web 大屏 | 读取服务端导出的 `dashboard.json`，用 ECharts 展示 |
| 机器学习扩展 | `IPredictionProvider` + 固定/规则生成的 1h、6h、24h Mock 结果 |

当前没有报修、人工客服工单、管理员账号管理、退款、预约违约、真实设备协议和模型训练平台。它们可以以后新增，但不能反向增加当前接口的实现负担。

用户已授权的只读 AI 问答按 [ADR 0005](../docs/decisions/0005-client-rag-assistant.md)
接入客户端预留页，遵循[本地智能助理边界](client-assistant-local.md)。它使用本地知识
和外部 HTTPS，不增加 V1 TCP 消息、数据库表或权威业务操作。

---

## 2. 总体调用边界

```text
用户 UI
  -> IChargingApi
       -> MockChargingApi                 开发期
       -> TcpChargingApi -> TCP Gateway   联调/演示

管理员 UI
  -> AdminFacade                          与服务端同进程，不走 TCP

TCP Gateway / AdminFacade
  -> ApplicationService
       -> Repository -> SQLite
       -> MockPile
       -> IPredictionProvider(Mock)
       -> DashboardExporter -> dashboard.json -> Web/ECharts
```

职责约定：

| 模块 | 负责 | 不负责 |
| --- | --- | --- |
| 用户/管理员 UI | 输入、页面跳转、展示和本地头像 | SQL、金额计算、权威状态修改 |
| `IChargingApi` | 给页面提供类型明确的用户端能力 | 数据库存取 |
| `TcpChargingApi` | JSON 转换、请求 ID、帧收发 | 业务判断 |
| `MockChargingApi` | 返回与真实 API 相同的 DTO 和错误 | 定义另一套页面逻辑 |
| TCP Gateway | 连接、分帧、解析、按 `type` 转发 | 直接写 SQL |
| `AdminFacade` | 给同进程管理员 UI 提供方法 | 绕过业务服务直接写表 |
| ApplicationService | 校验、业务状态、计费、事务编排 | QWidget 操作 |
| Repository | 参数化 SQL、联表和聚合 | JSON/TCP/UI 提示 |
| `MockPile` | 模拟开始、读数、停止、重启 | 自建硬件协议、心跳或命令日志 |

---

## 3. TCP 传输契约

只有用户端与服务端之间使用 TCP。管理员界面和服务端在同一个 `server-app` 进程内，通过 `AdminFacade` 调用相同业务服务。

### 3.1 帧格式

TCP 是字节流，必须保留一个最小分帧规则：

```text
[4 字节无符号消息体长度，大端序] + [UTF-8 JSON 消息体]
```

- 长度只计算 JSON 字节，不含 4 字节头；
- JSON 根节点必须是 object；
- 单帧最大 256 KiB；本协议不传头像或其他大文件；
- 接收端保留缓冲区，能够处理半帧和多帧粘连；
- 长度为 0、超限或正文不是合法 JSON 时，记录错误并断开该连接。

这只是 TCP 正确性要求，不代表系统需要处理高并发。

### 3.2 请求信封

```json
{
  "version": 1,
  "type": "station.list",
  "requestId": "req-1001",
  "token": "login-token",
  "data": {
    "longitude": 123.42,
    "latitude": 41.70,
    "region": "浑南区"
  }
}
```

| 字段 | 类型 | 必填 | 含义 |
| --- | --- | :---: | --- |
| `version` | integer | 是 | 当前固定为 `1` |
| `type` | string | 是 | 本文定义的消息名 |
| `requestId` | string | 是 | 客户端生成；只用于匹配响应 |
| `token` | string | 条件 | 登录和 ping 可省略，其余用户接口必填 |
| `data` | object | 是 | 没有参数时发送 `{}` |

`requestId` 不要求 UUID，不承担数据库幂等语义。可用进程随机前缀加递增序号。Demo 客户端不要自动重试充值、停止或支付请求。

### 3.3 响应信封

成功：

```json
{
  "version": 1,
  "type": "station.list",
  "requestId": "req-1001",
  "code": 0,
  "message": "OK",
  "data": {
    "items": []
  }
}
```

失败：

```json
{
  "version": 1,
  "type": "order.reserve",
  "requestId": "req-1002",
  "code": 40901,
  "message": "PILE_NOT_AVAILABLE",
  "data": {
    "pileCode": "PILE-A-02"
  }
}
```

响应必须回显请求的 `version/type/requestId`。`code == 0` 才表示成功；`data` 始终是 object。客户端按 `code` 分支，`message` 只用于默认提示和日志。

### 3.4 连接和处理方式

- 客户端主动连接配置中的主机和端口，并保持长连接；
- 服务端按帧到达顺序串行处理即可；不要求响应乱序和并发请求；
- 客户端可以只允许一个写操作处于“提交中”；按钮在响应前禁用；
- 普通请求建议 5 秒显示“服务不可用”，但本版不规定自动重试；
- 断线后重新连接、重新登录；token 只保存在服务端内存，服务重启后失效；
- 本版没有服务端主动 `EVENT`，进度由 `order.progress` 轮询。

---

## 4. 公共数据约定

### 4.1 单位和格式

| 数据 | JSON/数据库表示 | 示例 |
| --- | --- | --- |
| 金额 | integer，分 | `120` = 1.20 元 |
| 单价 | integer，分/kWh | `135` = 1.35 元/度 |
| 电量 | integer，Wh | `12500` = 12.5 kWh |
| 时长 | integer，秒 | `1800` = 30 分钟 |
| 功率 | number，kW | `60.0` |
| 经纬度/距离 | number，度/km | `123.42`、`2.35` |
| 时间 | UTC ISO 8601 string | `2026-09-02T19:00:00Z` |
| ID | integer | `stationId: 1` |
| 展示编号 | string | `pileCode: "PILE-A-01"` |

金额和电量都不用 JSON 浮点数。最终金额由服务端计算：

```text
amountCents = floor((energyWh * unitPriceCentsPerKwh + 500) / 1000)
```

### 4.2 枚举

| 枚举 | 值 |
| --- | --- |
| `UserStatus` | `ACTIVE`, `FROZEN` |
| `StationStatus` | `ACTIVE`, `DISABLED` |
| `PileType` | `FAST`, `SLOW` |
| `PileStatus` | `IDLE`, `RESERVED`, `CHARGING`, `FAULT`, `OFFLINE` |
| `OrderMode` | `RESERVATION`, `DIRECT` |
| `OrderStatus` | `RESERVED`, `CHARGING`, `PENDING_PAYMENT`, `COMPLETED`, `CANCELLED` |
| `CongestionLevel` | `LOW`, `MEDIUM`, `HIGH` |
| `PredictionSource` | `MOCK`, `MODEL` |

中文标签由界面映射，协议和数据库只使用上表英文值。

### 4.3 错误码

| code | message | 含义/客户端动作 |
| ---: | --- | --- |
| 0 | `OK` | 成功 |
| 40001 | `INVALID_REQUEST` | 字段缺失、类型或范围错误；提示用户修正 |
| 40101 | `INVALID_SESSION` | 缺 token、token 无效；返回登录页 |
| 40102 | `INVALID_CREDENTIALS` | 管理员账号或密码错误 |
| 40301 | `FORBIDDEN` | 用户被冻结、操作他人订单或管理员未登录 |
| 40401 | `NOT_FOUND` | 用户、站点、桩或订单不存在 |
| 40901 | `PILE_NOT_AVAILABLE` | 站点停用或桩不是 `IDLE`；刷新站点详情 |
| 40902 | `CURRENT_ORDER_EXISTS` | 用户已有预约、充电中或待支付订单；跳转当前订单 |
| 40903 | `ILLEGAL_ORDER_STATE` | 当前状态不能执行该动作；刷新订单 |
| 42201 | `INSUFFICIENT_BALANCE` | 补支付时余额不足；显示充值入口 |
| 50001 | `INTERNAL_ERROR` | 数据库或未分类服务端错误；显示通用提示 |
| 50301 | `SERVICE_UNAVAILABLE` | 客户端连接失败、断线或等待超时；允许提示后由用户重试 |

SQL、数据库路径、堆栈和密码哈希不得放进响应。

DTO 表中列出的字段在成功响应里都必须出现；标为 `/null` 的字段没有值时发送 JSON `null`。只有接口表中名字带 `?` 的字段才允许省略。这样 Mock 和 TCP 实现不会产生两种 JSON 形状。

---

## 5. 公共 DTO

### 5.1 `UserDto`

| 字段 | 类型 | 含义 |
| --- | --- | --- |
| `userId` | integer | 用户 ID |
| `phone` | string | 11 位手机号 |
| `nickname` | string | 昵称 |
| `balanceCents` | integer | 当前余额 |
| `status` | `UserStatus` | 用户状态 |
| `createdAt` | datetime | 注册时间 |

头像不属于该 DTO。客户端把本地 `avatarPath` 与 `UserDto` 组合展示。

### 5.2 `StationDto`

| 字段 | 类型 | 含义 |
| --- | --- | --- |
| `stationId` | integer | 站点 ID |
| `name` | string | 站名 |
| `region` | string | 区域 |
| `address` | string | 地址 |
| `longitude` / `latitude` | number | 站点坐标 |
| `priceCentsPerKwh` | integer | 当前站点价格 |
| `status` | `StationStatus` | 启用状态 |
| `totalPileCount` | integer | 实时聚合总桩数 |
| `availablePileCount` | integer | 站点为 `ACTIVE` 时的 `IDLE` 数量；停用站点固定为 0 |
| `onlineRatePercent` | number | 非 `OFFLINE` 数/总数 × 100 |
| `distanceKm` | number/null | 给出查询坐标时返回，否则为 null |
| `predictedCongestion` | `CongestionLevel`/null | Mock/模型给出的未来 1 小时拥堵等级 |
| `recommended` | boolean | 是否为本次列表中的低拥堵推荐站 |

### 5.3 `PileDto`

| 字段 | 类型 | 含义 |
| --- | --- | --- |
| `pileId` | integer | 电桩 ID |
| `stationId` | integer | 所属站点 |
| `pileCode` | string | 桩编号/扫码内容 |
| `pileType` | `PileType` | 快充/慢充 |
| `ratedPowerKw` | number | 额定功率 |
| `status` | `PileStatus` | 当前模拟状态 |
| `chargeCount` | integer | 已停止充电的订单数，即 `PENDING_PAYMENT + COMPLETED` |
| `totalChargeSeconds` | integer | 上述已停止订单的充电时长之和 |

### 5.4 `OrderDto`

| 字段 | 类型 | 含义 |
| --- | --- | --- |
| `orderId` | integer | 订单 ID |
| `orderNo` | string | 展示订单号 |
| `createdAt` | datetime | 订单创建时间；列表默认按它倒序 |
| `userId` | integer | 所属用户；服务端从 token 确定，客户端不得指定 |
| `stationId` / `stationName` | integer/string | 由电桩关联得到 |
| `pileId` / `pileCode` | integer/string | 使用的电桩 |
| `mode` | `OrderMode` | 预约或直接充电 |
| `status` | `OrderStatus` | 当前状态 |
| `reservedAt` | datetime/null | 预约时间 |
| `startedAt` | datetime/null | 开始时间 |
| `endedAt` | datetime/null | 停止时间 |
| `paidAt` | datetime/null | 支付时间 |
| `durationSeconds` | integer | 当前/最终时长 |
| `energyWh` | integer | 当前/最终电量 |
| `unitPriceCentsPerKwh` | integer/null | 开始时冻结的价格 |
| `amountCents` | integer | 当前预估或最终金额 |

### 5.5 `DashboardDto`

| 字段 | 类型 | 含义 |
| --- | --- | --- |
| `todayRevenueCents` | integer | 今日已完成订单金额 |
| `monthRevenueCents` | integer | 本月已完成订单金额 |
| `totalRevenueCents` | integer | 全部已完成订单金额 |
| `stationCount` | integer | 站点数 |
| `pileCount` | integer | 电桩数 |
| `pileStates` | object | `{idle, inUse, fault}` |
| `revenuePoints` | array | `[{date, revenueCents}]`；管理端按 `days` 返回 7/30 项，Web 快照固定 30 项 |
| `predictions` | array[`PredictionDto`] | 管理端/Web 要展示的 Mock 预测，可为空 |
| `generatedAt` | datetime | 聚合时间 |

其中 `inUse = RESERVED + CHARGING`，`fault = FAULT + OFFLINE`；`stationCount/pileCount` 统计表中全部记录，包括停用站点和故障/离线桩。
所有“今日、本月、自然日”按 `Asia/Shanghai`（UTC+8）划分；时间字段本身仍传 UTC。
`revenuePoints` 必须覆盖截至当日（包含当日）的连续 `N` 个中国业务自然日，`date` 固定为 `YYYY-MM-DD`，按 `date ASC` 排列；没有收入的日期补 0。

---

## 6. 用户端 TCP 接口

所有接口的请求参数都位于请求信封的 `data`，响应字段都位于响应信封的 `data`。列表数据量很小，当前全部一次返回，不分页。

### 6.1 系统、登录和资料

| type | token | 请求 data | 成功响应 data | 含义 |
| --- | :---: | --- | --- | --- |
| `system.ping` | 否 | `{echo?: string}` | `{echo?: string, serverTime: datetime}` | 检查 TCP/分帧/JSON 链路 |
| `auth.user.login` | 否 | `{phone: string}` | `{token: string, isNewUser: bool, user: UserDto}` | 只接受 11 位数字；存在则登录，不存在则按默认昵称建用户 |
| `auth.logout` | 是 | `{}` | `{success: true}` | 删除内存 token；断线也允许直接丢弃本地 token |
| `user.profile.get` | 是 | `{}` | `{user: UserDto}` | 刷新资料和余额 |
| `user.profile.update` | 是 | `{nickname: string}` | `{user: UserDto}` | 只修改昵称；昵称长度只接受 1..32 个字符 |
| `wallet.recharge` | 是 | `{amountCents: integer}` | `{balanceCents: integer}` | 模拟充值；金额只接受 1..1,000,000 分 |

登录默认昵称：`"用户" + phone.right(4)`。被冻结用户登录返回 `40301`。

头像流程完全在客户端：选择本地图片 -> 复制到应用数据目录 -> 用 `QSettings` 保存相对路径 -> 刷新页面。它不经过 TCP。

### 6.2 站点和电桩

#### `station.list`

请求：

| 字段 | 类型 | 必填 | 含义 |
| --- | --- | :---: | --- |
| `longitude` / `latitude` | number | 否 | 必须同时给出，经度 -180..180、纬度 -90..90；仅用于本次距离计算 |
| `region` | string | 否 | 区域筛选 |
| `keyword` | string | 否 | 匹配站名或地址 |

响应：`{items: StationDto[]}`。

- 用户端只返回 `ACTIVE` 站点；
- 有坐标时按 `distanceKm ASC`，无坐标时按 `stationId ASC`；
- 坐标不写数据库，也不写日志；
- 地址转坐标由客户端地图适配器完成，再调用本接口。
- 服务端用未来 1 小时的 Mock/模型结果填充 `predictedCongestion`；从低拥堵站中选一个 `recommended=true`，并列时优先距离更近者。预测暂不可用时所有站返回 `predictedCongestion=null,recommended=false`，不影响普通站点列表。

#### `station.detail`

请求：`{stationId: integer}`。

响应：`{station: StationDto, piles: PileDto[]}`。

只返回该站所有 Mock 电桩及当前状态。当前价格来自站点，不按桩型或时段再计算。

#### `prediction.latest`

请求：`{stationId: integer, horizonHours: 1|6|24}`。

响应：`{prediction: PredictionDto}`。该接口只读取 `IPredictionProvider`，不要求预测结果入库。用户端可用它展示推荐依据；非法时长返回 `40001`。

### 6.3 订单主流程

| type | 请求 data | 成功响应 data | 说明 |
| --- | --- | --- | --- |
| `order.current` | `{}` | `{order: OrderDto/null}` | 查询当前唯一的预约、充电中或待支付订单 |
| `order.reserve` | `{pileCode: string}` | `{order: OrderDto}` | 预约一个 `IDLE` 桩；当前没有自动过期 |
| `order.cancel` | `{orderId: integer}` | `{order: OrderDto}` | 仅 `RESERVED` 可取消；同时释放电桩 |
| `order.start` | `{pileCode: string, reservationOrderId?: integer}` | `{order: OrderDto}` | 预约转充电，或不带预约 ID 直接充电 |
| `order.progress` | `{orderId: integer}` | `{order: OrderDto, measuredAt: datetime}` | 从 Mock 取得当前时长、电量和预估金额 |
| `order.stop` | `{orderId: integer}` | `{order: OrderDto, paid: bool, balanceCents: integer, shortfallCents?: integer}` | 停止、出最终账单、释放桩；余额够时同事务自动结算 |
| `order.pay` | `{orderId: integer}` | `{order: OrderDto, balanceCents: integer}` | 充值后补付 `PENDING_PAYMENT` 订单 |
| `order.list` | `{}` | `{items: OrderDto[]}` | 当前用户全部订单，按 `createdAt DESC` |

重要语义：

1. 服务端从 token 确定 `userId`；请求不允许提交用户 ID。所有带 `orderId` 的接口都要验证订单属于当前用户，不属于时返回 `40301`；`order.list` 只返回当前用户订单。
2. 用户有 `RESERVED/CHARGING/PENDING_PAYMENT` 订单时，不能再预约或直接开始另一单。
3. 只有站点 `ACTIVE` 且桩 `IDLE` 才可预约/直接开始。
4. 预约没有课程说明书规定的有效期，本版不擅自增加 30 分钟超时和违约规则。
5. `order.start` 将站点当前 `priceCentsPerKwh` 写入订单；之后调价不影响该订单。
6. `order.progress` 不需要每秒写数据库。Mock 可基于开始时间和额定功率生成单调增长读数。
7. `order.stop` 必须先停止并释放桩。余额不足时仍成功返回 `PENDING_PAYMENT` 和差额，客户端引导充值；这不是网络错误。
8. `order.pay` 再次余额不足时返回 `42201`，订单保持 `PENDING_PAYMENT`。
9. 带 `reservationOrderId` 开始充电时：订单不存在返回 `40401`，属于其他用户返回 `40301`；属于当前用户但不是 `RESERVED`，或预约桩与请求 `pileCode` 不一致，返回 `40903`。
10. 客户端不得提交电量、最终时长、单价、金额或目标状态。
11. `order.cancel` 只接受 `RESERVED`；`order.progress` 和 `order.stop` 只接受 `CHARGING`；`order.pay` 只接受 `PENDING_PAYMENT`。状态不符统一返回 `40903`。
12. 任何服务方法返回 `CHARGING` 的 `OrderDto` 时，都先向 `MockPile` 读取一次当前读数并现算时长、电量和预估金额；过程值无需写库。

实现对接：服务端已接入本节全部 8 个 V1 消息，详见[订单联调说明](../server/order-flow.md)。未改变信封、DTO、五种订单状态或 `stop` 自动结算语义。补充 fixture 见[示例索引](examples/README.md)，包含无当前订单、取消、待支付停止、补付、历史列表和归属拒绝。

---

## 7. 管理端进程内接口：`AdminFacade`

管理员 UI 位于 `server-app` 内，不需要管理端 TCP、管理员 token、RBAC 或站点授权表。所有方法仍通过 ApplicationService，不能直接写数据库。

当前 Demo 的逻辑调用是同步返回，统一结果形状为 `{code: integer, message: string, data: T}`。`code == 0` 时读取 `data`；失败时只读取 `code/message`，`data` 不承载业务结果。具体使用 Qt 模板、结构体还是其他表示由服务端负责人决定。

管理端额外使用三个小类型：

```text
AdminInfoDto       = {adminId: integer, displayName: string}
StationDetailDto   = {station: StationDto, piles: PileDto[]}
StationCreateInput = {
  name: string[1..64], region: string[1..64], address: string[1..200],
  longitude: number[-180..180], latitude: number[-90..90],
  priceCentsPerKwh: integer > 0,
  piles: [{pileCode: string[1..64], pileType: FAST|SLOW,
           ratedPowerKw: number > 0 and <= 1000}]
}
```

最小方法清单：

| 方法 | 输入 | 输出 | 含义 |
| --- | --- | --- | --- |
| `login` | `username, password` | `Result<AdminInfoDto>` | 校验 `admins.password_hash`；初始账号 `admin/123456`，凭证错误返回 `40102` |
| `getDashboard` | `days: 7 或 30`，或管理端内部 `startDate, endDate` | `Result<DashboardDto>` | KPI、营收曲线和桩状态比例；自定义范围不超过 366 个中国业务日 |
| `listStations` | `region?, keyword?` | `Result<StationDto[]>` | 管理端包含停用站点 |
| `getStation` | `stationId` | `Result<StationDetailDto>` | 站点和站内实时 Mock 状态 |
| `createStation` | `StationCreateInput` | `Result<StationDetailDto>` | 同一事务创建站点和指定数量的 Mock 桩 |
| `listPiles` | `stationId?, status?` | `Result<PileDto[]>` | 电桩管理列表及累计次数/时长 |
| `createPile` | `stationId, pileCode, pileType, ratedPowerKw` | `Result<PileDto>` | 在已启用站点中新建空闲电桩；所属站点必填，编号唯一 |
| `deletePile` | `pileId` | `Result<{success}>` | 仅删除无订单且处于 `IDLE/OFFLINE` 的电桩 |
| `setPileStatus` | `pileId, status: IDLE/OFFLINE/FAULT` | `Result<PileDto>` | 管理端上线、下线或标记故障；不能干预使用中电桩 |
| `restartPile` | `pileId` | `Result<PileDto>` | 调用 Mock；仅 `IDLE/OFFLINE` 可重启为 `IDLE` |
| `listUsers` | `phoneKeyword?` | `Result<UserDto[]>` | 支持手机号包含查询 |
| `setUserStatus` | `userId, UserStatus` | `Result<UserDto>` | 冻结/解冻；业务请求每次读取状态 |
| `listOrders` | `userId?, stationId?, status?` | `Result<OrderDto[]>` | 查看完整充电过程和账单字段 |
| `getPrediction` | `stationId, horizonHours: 1/6/24` | `Result<PredictionDto>` | 管理端负荷曲线和高拥堵预警 |

约定：

- `createStation` 的 `piles` 明细由管理员端在提交前填写，服务端生成站点 ID、桩 ID、创建时间和初始 `IDLE` 状态；站点与全部初始电桩必须同一事务提交，响应必须返回实际结果。`piles` 为空表示创建空站点；
- `createPile` 的 `pileId` 由服务端生成，初始状态为 `IDLE`，站点桩数通过实时聚合变化，不在站点表维护计数；
- `deletePile` 不删除历史：存在任何订单、处于 `RESERVED/CHARGING/FAULT` 时返回 `40903`；
- `setPileStatus` 不创建命令表或审计表；`OFFLINE -> IDLE` 表示上线，`IDLE -> OFFLINE` 表示下线，`IDLE/OFFLINE -> FAULT` 表示故障；普通管理操作不能将 `FAULT` 恢复；
- `restartPile` 不创建命令表或审计表；`OFFLINE` 变为 `IDLE`，`IDLE` 返回成功且状态不变，`RESERVED/CHARGING/FAULT` 返回 `40903`；
- 管理端只需要一个管理员身份；没有新增管理员、角色分配或改密接口；
- 冻结用户不删除其历史订单和余额；用户存在 `RESERVED/CHARGING/PENDING_PAYMENT` 订单时暂不允许冻结，返回 `40902`，避免订单和电桩无人结束；
- `getDashboard(days)` 的 `revenuePoints` 必须按中国业务日补齐为恰好 7 或 30 个点；不需要日期表；
- 管理端自定义日期查询包含起止日，只改变 `revenuePoints`，今日/月/累计 KPI 的业务含义不变；
- `getPrediction` 返回 `HIGH` 时管理界面显示负荷预警，显示方式不是接口契约。

---

## 8. 外部、Mock 和扩展接口

### 8.1 客户端 API 切换

页面 Controller 只依赖 typed `IChargingApi`，不直接调用字符串 `type`。接口只冻结下列逻辑操作，不冻结 C++ 类声明、文件名或 Qt 信号名称：

| typed 操作 | 输入 | 成功 payload | 对应 TCP type |
| --- | --- | --- | --- |
| `ping` | `echo?` | `{echo?, serverTime}` | `system.ping` |
| `loginUser` | `phone` | `{token, isNewUser, user}` | `auth.user.login` |
| `logout` | 无 | `{success}` | `auth.logout` |
| `getProfile` / `updateNickname` | 无 / `nickname` | `{user}` | `user.profile.get/update` |
| `recharge` | `amountCents` | `{balanceCents}` | `wallet.recharge` |
| `listStations` / `getStation` | `StationQuery` / `stationId` | §6.2 的完整响应 data | `station.list/detail` |
| `getCurrentOrder` / `listOrders` | 无 | §6.3 的完整响应 data | `order.current/list` |
| `reserve` / `cancel` | `pileCode` / `orderId` | §6.3 的完整响应 data | `order.reserve/cancel` |
| `start` | `pileCode, reservationOrderId?` | §6.3 的完整响应 data | `order.start` |
| `progress` / `stop` / `pay` | `orderId` | §6.3 的完整响应 data | `order.progress/stop/pay` |
| `getPrediction` | `stationId, horizonHours` | `{prediction: PredictionDto}` | `prediction.latest` |

每次调用立即返回一个 `requestId`，随后通过实现者选择的完成事件/回调恰好返回一次：

```text
ApiResult = {requestId, type, code, message, payload}
```

- 成功时 `payload` 是对应表格中**整个响应 `data`** 转换得到的 typed 对象，而不是其中随意抽取的单个字段；
- 失败时 `payload` 为空，页面只读取 `code/message`；
- 本地参数校验失败映射为 `40001 / INVALID_REQUEST`；连接失败、断线和等待超时映射为 `50301 / SERVICE_UNAVAILABLE`；
- 实现可另行通知连接状态变化，但页面业务不能依赖某个具体 Qt 信号名称；
- `StationQuery` 只含 §6.2 的坐标、区域和关键词。

两个实现：

- `MockChargingApi`：早期页面开发，返回与本文完全相同的字段；
- `TcpChargingApi`：联调和最终演示，负责长度头和 JSON。

页面不得用 `#ifdef` 写两套业务逻辑。启动参数或配置选择 `mock/real`。

API 适配器持有登录态：`auth.user.login` 成功后保存响应 token，之后给需要鉴权的请求自动附加；`auth.logout` 成功、连接断开或收到 `40101` 时清除。Controller 不拼 token，也不读取 TCP 信封。`ApiResult.type` 使用 §6 中对应的消息 `type`，因此多个并行页面可以按 `requestId + type` 对应结果。

每个返回了 `requestId` 的调用都必须且只能产生一次完成结果，包括本地参数校验失败和网络失败；这样页面不会永久停在“提交中”。

`TcpChargingApi` 内部可以再使用一个通用 `send(type, data)` 传输对象，但该对象不暴露给页面。本文不规定文件数量和类目录。

### 8.2 `MockPile`

当前唯一充电桩实现：

```text
start(pileId, startedAt) -> success/error
read(pileId, startedAt, now) -> {durationSeconds, energyWh}
stop(pileId, startedAt, now) -> {durationSeconds, energyWh}
restart(pileId) -> success/error
```

约定只有三条：

1. 同一次充电的 `durationSeconds` 和 `energyWh` 不倒退；
2. `stop` 返回最终读数，此后由订单保存；
3. `restart` 对 `OFFLINE` 返回成功并恢复 `IDLE`，对 `IDLE` 成功且状态不变，对 `RESERVED/CHARGING/FAULT` 返回 `40903`；故障恢复留给后续报修流程，不模拟真实厂商协议、确认消息、心跳、超时或命令历史。

以后接真实设备时，可以让真实实现提供相同四项能力，ApplicationService 不需要改接口。

### 8.3 地图和扫码

这些能力在用户客户端，不经过业务数据库：

```text
IMapService.geocode(address) -> {longitude, latitude}
IMapService.openRoute(start, end, mode) -> success/error
IScanner.scan() -> pileCode
```

- 默认允许 `MockMapService` 返回演示坐标和静态路线；
- 有腾讯地图 Key 和网络时切到真实适配器；
- 客户端本地 `RouteMode` 接受 `DRIVING`、`WALKING`、`TRANSIT`（公共交通）、`CYCLING`（自行车骑行）；
  它不出现在 TCP 信封或共享业务 DTO 中。`TRANSIT/CYCLING` 是兼容增加，既有驾车/步行含义不变；
- 真实适配器在 `QWebEngineView` 中展示腾讯地图路线：驾车使用 URI `type=drive`，
  公共交通使用 `type=bus`；步行和骑行分别使用 WebService `walking/bicycling`，
  在 JavaScript 地图中绘制返回的压缩折线，展示服务返回的米数和预估分钟数。
  公共交通采用腾讯默认策略，公交/地铁换乘和无可用路线提示由腾讯页面提供；
- 路线查询由客户端直接访问腾讯，不新增项目服务端接口或数据库字段。从订单进入导航时，
  仍通过既有 `station.detail` 获取站点地址与坐标；路线结果不改变订单状态或计费；
- Mock 公共交通和骑行只返回明确标注的离线摘要，不伪造公交线路、班次或票价。
  缺少 Key、无网络或页面加载失败时显示错误，不回退为成功 Mock；
- 决策与边界见 [ADR-0003](../docs/decisions/0003-client-transit-navigation.md)，
  骑行增量见 [ADR-0004](../docs/decisions/0004-client-cycling-navigation.md)。本地输入示例见
  [公共交通](examples/map-route.transit.local.json)和[骑行](examples/map-route.cycling.local.json)（均非 TCP 消息）；
- 扫码不可用时允许手输 `pileCode`；
- Key 从环境或本地配置读取，不写数据库和接口 fixture。

### 8.4 预测扩展

为了保留说明书中的机器学习展示点，冻结一个小接口，不建训练平台或预测表：

```text
IPredictionProvider.predict(stationId, horizonHours)
  -> PredictionDto
```

`horizonHours` 只接受 `1/6/24`。`PredictionDto` 同时覆盖说明书要求的负荷、空闲桩和高峰时段：

```json
{
  "stationId": 1,
  "horizonHours": 6,
  "generatedAt": "2026-09-02T19:00:00Z",
  "source": "MOCK",
  "peakStartAt": "2026-09-02T21:00:00Z",
  "peakEndAt": "2026-09-02T22:00:00Z",
  "congestionLevel": "MEDIUM",
  "points": [
    {
      "time": "2026-09-02T20:00:00Z",
      "loadKw": 42.5,
      "availablePiles": 2,
      "congestionLevel": "MEDIUM"
    }
  ]
}
```

正式字段约定：

| 字段 | 类型 | 含义 |
| --- | --- | --- |
| `stationId` | integer | 被预测的站点 ID |
| `horizonHours` | `1/6/24` | 从下一整点开始的预测小时数 |
| `generatedAt` | datetime | 本次结果生成时间 |
| `source` | `PredictionSource` | 当前为 `MOCK`；以后真实模型为 `MODEL` |
| `peakStartAt` / `peakEndAt` | datetime/null | 区间内高峰的起止；必须同时有值或同时为 null |
| `congestionLevel` | `CongestionLevel` | 整个预测区间的总体拥堵级别 |
| `points` | array[`PredictionPointDto`] | 恰好包含 `horizonHours` 个点 |

`PredictionPointDto = {time: datetime, loadKw: number >= 0, availablePiles: integer >= 0, congestionLevel: CongestionLevel}`。点按 `time ASC` 排列；`availablePiles` 不得超过该站总桩数。若有高峰起止，它们必须位于预测区间内且 `peakStartAt < peakEndAt`。

`points` 从下一整点开始、相邻点间隔 1 小时，并严格返回 `horizonHours` 个点，即分别为 1、6 或 24 个；每个点都包含示例中的四个字段。

`peakStartAt/peakEndAt` 在预测区间内没有明显高峰时都为 null。当前 `MockPredictionProvider` 可根据站点桩数和固定曲线生成结果。它有三个消费者：用户站点推荐及 `prediction.latest`、管理员 `getPrediction` 预警、Web 快照。未来真实 Python 模型只需输出同一 DTO；是否保存预测历史届时再决定。

### 8.5 Web 大屏快照

Web 页面不打开 `.db` 文件。服务端通过按钮或简单 `QTimer` 调用：

```text
DashboardExporter.export(path) -> success/error
```

该方法始终导出最近 30 个中国业务日并补齐零收入日期；Web 切换到“7 日”时只取数组最后 7 项。因此导出器不需要再暴露日期参数。

输出 `dashboard.json`：

```json
{
  "schemaVersion": 1,
  "generatedAt": "2026-09-02T19:00:00Z",
  "summary": {
    "todayRevenueCents": 2160,
    "monthRevenueCents": 2880,
    "totalRevenueCents": 15690,
    "stationCount": 3,
    "pileCount": 12
  },
  "pileStates": {"idle": 7, "inUse": 2, "fault": 3},
  "revenuePoints": [
    {"date": "2026-09-02", "revenueCents": 2160}
  ],
  "predictions": [
    {"stationId": 1, "horizonHours": 1, "congestionLevel": "LOW"}
  ]
}
```

上例只展示数组元素的形状；实际 `revenuePoints` 必须有 30 项，实际预测元素使用完整 `PredictionDto`。ECharts 只依赖该 JSON。刷新频率、图表颜色和 HTTP 静态服务器不是业务契约。

为控制 Demo 快照体积，V1 的 `predictions` 只放一个代表站点的完整 1 小时 `PredictionDto`：选择 `stationId` 最小的 `ACTIVE` 站点；没有启用站点或 Provider 暂不可用时返回空数组。其他站点和 6/24 小时结果仍通过用户端 `prediction.latest` 或管理端 `getPrediction` 按需取得。

---

## 9. 业务规则与数据库映射

| 接口/方法 | 读写表 | 最小原子边界 |
| --- | --- | --- |
| 用户登录/资料/充值 | `users` | 自动注册一次插入；充值一次余额更新 |
| 管理员登录 | `admins` | 只读 |
| 站点列表/详情 | `charging_stations`, `charging_piles`, `charging_orders` | 只读聚合 |
| 预约 | `charging_orders`, `charging_piles`, `users` | 插订单和占桩同一事务 |
| 开始充电 | `charging_orders`, `charging_piles`, `charging_stations` | 状态和价格快照同一事务 |
| 进度 | 订单、桩 + `MockPile` | 只读；无需持续写表 |
| 停止并结算 | `charging_orders`, `charging_piles`, `users` | 最终账单、释放桩、可选扣款同一事务 |
| 补支付 | `charging_orders`, `users` | 扣余额和完成订单同一事务 |
| 管理新增站点 | `charging_stations`, `charging_piles` | 新站和初始 Mock 桩同一事务 |
| 管理重启桩 | `charging_piles` + `MockPile` | 一次状态更新 |
| 预测/推荐/预警 | `IPredictionProvider` | 当前只读 Mock，无数据库表 |
| Dashboard/Web | 全部核心表的只读聚合 | 无写业务数据 |

界面提交的数据一律视为输入，不是事实。以下字段由服务端决定：用户 ID、订单状态、桩状态、充电量、时长、单价、金额、支付结果和营收。

---

## 10. 线程模型约定

课程说明书要求“主框架体现多线程”，但没有性能指标。本项目只保留一个极薄边界：

```text
主线程：QWidget 管理员 UI + TCP 事件
    |
    | direct call（默认 Demo）
    | 或 Qt::QueuedConnection（需要展示 QThread 时）
    v
AppCore：ApplicationService + Repository + Mock + 单 SQLite 连接
```

- 默认可以让 `AppCore` 与主线程同线程运行，业务仍全部串行；
- `AdminFacade` 默认同步调用 `ApplicationService`，这也是当前冻结的运行方式；架构图中的 QThread 是可选边界；
- 若答辩临时要求实际演示线程，可在同步 Service 外加请求/完成信号适配器，再把整个 `AppCore` 移到一个 `QThread`；投递方式会变成异步，但上文业务操作、参数和 `Result/DTO` 语义不变；
- 启用 QThread 后只在线程间传 DTO/值对象，数据库连接在 AppCore 所在线程中创建；
- 不使用线程池、读写锁、连接池、写队列或并发压力测试；
- QWidget 永远只在主线程操作。

这条边界的目标是满足课程结构展示和保留扩展能力，不是宣称高并发。

---

## 11. 联调与验收

### 11.1 用户端主链路

1. `system.ping` 验证帧收发；
2. 使用 `13800000001` 登录；
3. `station.list -> station.detail`；
4. 对 `PILE-A-01` 执行 `order.reserve -> order.start`；
5. 两次调用 `order.progress`，确认读数不倒退；
6. `order.stop`，确认订单完成或进入待支付，且桩回到 `IDLE`；
7. 如待支付，先 `wallet.recharge` 再 `order.pay`；
8. `order.list` 能看到完整时间、电量、价格和金额。

### 11.2 管理端和大屏

1. `admin / 123456` 登录；
2. 7 日/30 日营收曲线均可显示；
3. 桩状态数量与数据库一致；
4. 手机号模糊查询和冻结/解冻可用；
5. 新增站点后能看到自动生成的 Mock 桩；
6. 故障/离线桩执行模拟重启后变为 `IDLE`；
7. 导出的 `dashboard.json` 能被 ECharts 页面读取；
8. 1/6/24 三种 Mock 预测都能返回格式一致的数据。

### 11.3 必测失败分支

- 非 11 位手机号 -> `40001`；
- 冻结用户登录/调用业务 -> `40301`；
- 预约正在充电、预约、故障或离线桩 -> `40901`；
- 已有当前订单又预约另一桩 -> `40902`；
- 对非 `RESERVED` 订单取消 -> `40903`；
- 补支付余额不足 -> `42201`；
- 非法帧长度或非法 JSON -> 断开连接，不崩溃；
- 数据库错误 -> `50001`，响应不泄露 SQL。

---

## 12. 兼容与扩展规则

- 响应 `data` 可以增加客户端可忽略的可选字段；
- 可以增加新的 `type`，现有客户端不调用即可；
- 不要在版本 1 中重命名字段、改变类型/单位或改变已有状态含义；
- 真正需要破坏性修改时把 `version` 升为 2；
- 新增真实地图、真实桩、真实模型时，应替换适配器实现，不让 UI 或 Repository 改用另一套契约；
- 新增报修、客服、钱包流水、管理员角色等功能时，单独增加表和接口，不把它们塞进现有订单或用户 JSON 字段。

本文刻意不规定类的数量、具体回调/信号名称、SQL 查询文本、日志库和测试框架；仓库一级的团队责任目录以根 README 为准。后续 AI 可以自由实现目录内部细节，只要保持上述接口语义、数据单位、状态转换和模块边界。
