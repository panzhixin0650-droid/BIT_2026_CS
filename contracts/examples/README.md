# 核心协议 fixture

本目录中的 JSON 是客户端 Mock 与服务端协议测试共用的固定输入，不是数据库种子，也不定义契约之外的新业务语义。

## 成功主链路

按以下顺序使用：

1. `system-ping.request.json` / `system-ping.response.json`
2. 现有 `auth-user-login.request.json` / `auth-user-login.response.json`
3. `station-list.request.json` / `station-list.response.json`
4. `station-detail.request.json` / `station-detail.response.json`
5. `order-reserve.request.json` / `order-reserve.response.json`
6. `order-start.request.json` / `order-start.response.json`
7. `order-progress-1.request.json` / `order-progress-1.response.json`
8. `order-progress-2.request.json` / `order-progress-2.response.json`
9. `order-stop.request.json` / `order-stop.response.json`

主链路固定使用用户 `userId=1`、token `fixture-token`、站点 `stationId=1`、电桩 `PILE-A-01` 和订单 `orderId=1001`。状态依次变化为：

- `PILE-A-01`：`IDLE -> RESERVED -> CHARGING -> IDLE`
- 订单 `1001`：`RESERVED -> CHARGING -> COMPLETED`

两次进度响应的时长、电量和金额单调增长。最终电量为 `5000 Wh`，冻结单价为 `135 分/kWh`，按 V1 契约公式得到 `675 分`，从初始余额 `20000 分` 扣除后剩余 `19325 分`。

## 独立失败场景

以下 fixture 各自依赖独立前置状态，不应插入上面的成功链路：

- `station-list.invalid-session.*.json`：请求缺少 token，返回 `40101 INVALID_SESSION`。
- `order-reserve.pile-not-available.*.json`：`PILE-A-02` 已在充电，返回 `40901 PILE_NOT_AVAILABLE`。
- `order-pay.insufficient-balance.*.json`：订单 `2001` 已是 `PENDING_PAYMENT` 且余额不足，返回 `42201 INSUFFICIENT_BALANCE`。

## 客户端本地地图示例

[`map-route.transit.local.json`](map-route.transit.local.json) 描述本地 `IMapService`
公共交通输入与腾讯 URI 模式映射，**不是 TCP 请求/响应**，没有 token 或 Key，不能发送到
项目服务端。成功/失败通过本地地图完成事件返回；Mock 成功只表示生成了离线摘要。

[`map-route.cycling.local.json`](map-route.cycling.local.json) 是本地自行车骑行输入及腾讯
响应形状的测试示例。坐标、距离和用时为测试数据，不代表真实可骑行路线；它不发送到
项目服务端，不含真实 Key。用于验证骑行接口选择、压缩折线解码与地图展示。
