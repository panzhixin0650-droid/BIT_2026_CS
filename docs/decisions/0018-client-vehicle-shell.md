# ADR 0018：独立车载横屏用户端与双端共享边界

状态：IMPLEMENTED（2026-09-08，用户确认设计后授权实现）。

## 背景与目标

现有 `user-client` 是 360×640 起的手机式 Qt Widgets 用户端，主窗口同时组织首页、
充电、扫一扫、客服助理和我的五个底部入口。课程 Demo 现在需要在同一 `client/` 模块中
增加独立的 `vehicle-client`：它面向 1280×720 横屏，最低验收 1024×600，与手机端
共享业务和地图能力，但拥有独立主窗口、页面布局、导航和 UI 测试。

本决策只增加客户端装配和展示，不改变服务端、数据库、V1 TCP 消息、共享 DTO、金额/
电量单位或订单状态语义。头像继续是每个客户端本地数据；当前位置继续是请求级数据。

## 页面范围与流转

车载底部主导航严格只有三个入口：`首页`、`充电`、`我的`。不提供扫一扫、摄像头、
二维码图片识别或相册扫码入口，也不链接 Qt Multimedia 或 ZBar。客服助理作为“我的”
二级入口；电桩报修可从充电页或“我的”进入，工单列表与管理员回复从“我的”进入。

```text
登录
  └─ 首页（地图约 60%～70% + 右侧发现/站点面板）
       ├─ 修改当前位置 ──> 重新请求 station.list
       ├─ 搜索/推荐/附近/最近使用 ──> 站点预览/详情
       │                                  ├─ 路线规划
       │                                  ├─ 预约 ───────┐
       │                                  └─ 选桩 ───────┤
       └──────────────────────────────────────────────────┤
                                                          v
充电 <── 底栏/当前订单 ── 无订单 / 已预约 / 启动确认 / CHARGING
  ├─ 每秒 order.progress ──> 充电进度、功率、电量、时长、金额
  ├─ 二次确认停止 ─────────> COMPLETED / PENDING_PAYMENT
  ├─ 自动结束后刷新 ───────> 历史最终状态 / 待支付
  └─ 当前桩报修 / 前往充值

我的
  ├─ 资料详情（昵称、手机号、本地头像）
  ├─ 余额与充值 / 我的订单
  ├─ 客服助理 / 故障报修 / 我的工单
  └─ 退出登录 ──> 登录
```

首页站点 Marker 与右侧站点卡调用同一个选站动作，得到同一预览和详情状态。站点详情在
右侧面板内切换，避免手机式全屏覆盖。路线页复用同一地图能力与既有驾车、步行、公交、
骑行模式，车载默认驾车；它仍只是路线规划和交互查看，不声称持续 GPS、语音播报、偏航
检测、自动重算、实时交通保证或基于车辆剩余电量推荐。

## 共享与独立代码边界

采用渐进提取，不一次移动全部手机端文件：

| 边界 | 内容 | 依赖限制 |
| --- | --- | --- |
| `charging_client_api` | `IChargingApi`、Mock/TCP 适配器、协议 DTO | 两端共享；不含 UI |
| `charging_client_common` | 主题 token、API 错误映射、地图服务与地图画布、地图类型、头像本地存储、可复用充电会话状态逻辑和通用控件 | 两端共享；不含扫码、手机主窗口 |
| `charging_client_mobile_ui` | 现有 `MainWindow`、手机页面、扫一扫/相册扫码、手机专属布局与助理入口组织 | 只供 `user-client` |
| `charging_client_vehicle_ui` | `VehicleMainWindow`、三入口横屏页面、右侧站点面板、车载资料/订单/客服二级页面 | 只供 `vehicle-client`；不引用扫码头文件 |

共享业务状态以不依赖具体 QWidget 页面类的协调器或小型状态模型表达。现有 Controller
若仍与手机 View 强耦合，先只提取车载实际需要的订单会话、请求匹配和格式化逻辑；不为了
目录理想化重写全部手机页面。两套主窗口都只通过 `IChargingApi` 和 `IMapService` 工作，
不引用 `server/`、不访问 SQLite，也不直接互相通信。

主题从现有森林绿/暖白 QSS 中提取有限 token：字体族、字体级别、颜色、间距、圆角、
阴影和 48/56 px 触控尺寸。两端共享 token，分别定义布局规则；优先使用
`Noto Sans CJK SC`，由系统字体回退处理缺失情况。

## CMake 目标与可选依赖

目标结构：

```text
charging_client_api
charging_client_common
├─ charging_client_mobile_ui ── user-client
└─ charging_client_vehicle_ui ─ vehicle-client
```

`CHARGING_CLIENT_ENABLE_WEBENGINE=ON` 时地图公共层链接 WebEngineWidgets/WebChannel。
`CHARGING_CLIENT_ENABLE_SCANNER=ON` 时 Qt Multimedia、ZBar 及扫码源文件只附加到
`charging_client_mobile_ui`；`charging_client_vehicle_ui` 和 `vehicle-client` 不传播、
不链接这两个依赖。保留 `--api mock|tcp`、`--map mock|tencent`、host、port 和 timeout
语义；两个 executable 使用各自的 main/应用名，防止本地设置与头像路径被误称为跨端同步。

预期命令：

```bash
cmake -S client -B build/client -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCHARGING_CLIENT_ENABLE_WEBENGINE=ON
cmake --build build/client --target user-client
cmake --build build/client --target vehicle-client
```

## 横屏布局与地图生命周期

- 默认窗口 1280×720，最小窗口 1024×600；正文、辅助文字与指标数字只使用有限字号层级。
- 首页主体为响应式左右分栏：地图目标占可用宽度 60%～70%，右侧面板可滚动；底栏固定在
  内容区下方，不覆盖地图署名、缩放/全景/附近控件。窄至 1024×600 时保持分栏并压缩间距，
  列表和详情通过滚动访问，不隐藏主要操作。
- 主要命中区域至少 48×48，开始/停止/支付等关键动作至少 56 px；停止必须二次确认。
- 切换三个主入口时隐藏而不销毁首页/路线 WebEngine 页面，保留地图实例、视野和路线；
  换站、退出路线或退出登录时才按 ADR 0006 清理路线状态。
- Mock 地图使用现有离线 OSM 资源且不联网；Tencent 缺 Key 或加载失败时明确失败，不回退为
  成功 Mock。Key 只来自环境或未入库本地配置。
- 没有车辆定位、车速、档位或总线接口，因此只显示“演示位置/用户确认位置”和可选的
  “请停车后操作”提示，不实现或宣称车辆 GPS、行驶锁定或真实 SOC。

## 双端一致性与异步响应

TCP 模式下，手机端和车载端是两个独立长连接与 token 会话，但都通过同一服务端读取同一
用户的权威数据。客户端之间不通信、不复制本地订单状态：

1. 登录成功，以及进入首页、充电或“我的”时主动刷新相关数据；程序恢复或网络重连后
   重新登录成功再刷新。没有服务端 EVENT，`CHARGING` 期间沿用约 1 秒轮询。
2. 每个页面/协调器保存当前 `requestId + type + session generation + view generation`。
   响应四者不匹配时丢弃；退出登录、切换账号、换站或离开详情会递增 generation 并清除
   pending request，迟到响应不能覆盖新用户或新页面。
3. 昵称、手机号、余额、订单和工单只展示 API 最新响应。昵称更新、充值、支付、预约、
   开始或停止成功后，重新调用受影响的 profile/current/list/detail 接口，不在 UI 预写权威值。
4. 一端停止后，另一端下一次 `order.progress` 若收到 `40903`，立即转为读取
   `order.current`，必要时读取 `order.list` 找回最终 `COMPLETED/PENDING_PAYMENT`。
   双端同时停止时，先到请求执行事务；后到请求的 `40903` 只触发刷新，不重复显示成功、
   不自动重试停止，也不重复扣费。
5. 自动结束遵循服务端 180 秒策略。圆环标签固定为“充电进度”或“Demo 会话进度”；
   `durationSeconds`、`energyWh`、`amountCents` 和订单状态只来自服务端 DTO，功率展示使用
   服务端订单/桩数据可以直接支持的值，不由车载 UI 构造最终读数。
6. `40101 INVALID_SESSION` 统一清空本地 token、页面 pending 状态与账号数据并回登录页；
   `50301`/超时/断线显示可恢复错误，禁止回退到 Mock 成功。写操作结果不确定时刷新核对，
   不自动重发充值、开始、停止或支付。

Mock 模式只能保证单个进程内与 typed API 一致，不能模拟两个独立进程共享权威状态；双端
一致性验收必须使用 TCP 和同一服务端。头像按应用名和用户 ID 保存在各自客户端本地，页面
明确标注“本机头像”，不宣称跨设备同步。

## 测试计划

自动化测试分层：

1. CMake/链接：分别构建两个 executable；在 scanner 开启时检查 `vehicle-client` 的链接依赖
   不含 Qt Multimedia/ZBar，且源码/对象不含扫码页面。
2. 车载 UI：登录后三个且仅三个底栏标签；无扫码入口；1280×720、1024×600 和高 DPI 下
   检查可见控件几何、滚动可达、48/56 px 命中区、地图/右栏/底栏/署名互不遮挡。
3. 车载流程：Marker/列表统一选站、详情、预约、启动确认、每秒进度、二次确认停止、自动
   结束、待支付、充值、资料、订单、客服/报修/工单、退出登录。
4. 异常恢复：延迟响应、换页/换账号、断线、超时、`40101`、`40903` 和地图缺 Key；验证
   TCP 失败不回退 Mock，Mock 地图不访问网络。
5. 双客户端 TCP：两个独立 `IChargingApi`/窗口登录同一用户，验证共享当前订单、停止后的
   下一次刷新、并发停止只有一次成功、自动结束恢复，以及昵称/余额/订单/工单刷新一致；
   头像分别保留本地且界面不声称同步。
6. 回归：现有 `user-client` 构建及 18 个测试集继续通过；WebEngine 用真实桌面或 Xvfb，
   不强制 Qt 6.2 offscreen；真实 Tencent SDK/Key、WebGL 与高 DPI 做人工烟测。
7. 质量：`git diff --check`，Markdown 相对链接检查，确认无 Key、构建物、数据库或 Qt 本地
   配置进入变更。

## 预计实现文件

采用实际需要再细化文件名，预计修改/新增：

- `docs/decisions/0018-client-vehicle-shell.md`、`docs/INDEX.md`、`client/README.md`；
- `client/CMakeLists.txt`，以及独立 `src/vehicle_main.cpp`；
- `client/src/common/` 下主题 token、会话协调与必要共享组件；
- `client/src/vehicle/` 下主窗口、首页、充电、我的及二级页面；
- 必要的现有 `client/src/ui/` 和 `client/src/main.cpp` 小范围装配调整，不改变手机布局/行为；
- `client/tests/vehicle_*.cpp` 与必要 TCP 双客户端 fixture/测试。

不预计修改 `server/`、`database/`、`shared/protocol/`、`contracts/overall-interface-v1.md` 或
`docs/extension/`。若实现中发现现有 V1 无法表达某项权威数据，停止该项实现并先提出契约/
存储增量方案，不在客户端伪造。

## 结论

该方案是对现有 V1 客户端消费者的增加，不是契约扩张。它复用已冻结的 typed API、DTO、
错误码、地图边界和服务端订单事务；新增内容仅为客户端 target、横屏交互、共享代码提取与
测试。服务端、数据库和 V1 协议无需修改。

## 实施与验证记录

- 已增加 `vehicle-client`、`charging_client_common`、`charging_client_mobile_ui` 和
  `charging_client_vehicle_ui`；现有测试继续通过兼容 target 名称链接手机 UI。
- 当前同时启用 WebEngine 和 scanner 的构建中，`user-client`、`vehicle-client` 均独立
  构建成功；`ldd vehicle-client` 不包含 Qt Multimedia 或 ZBar，车载源码也无扫码引用。
- 车载普通和 1.5× 高 DPI UI 测试通过，覆盖 1280×720、1024×600、三个底栏入口、
  无扫码、左右分栏、触控尺寸、选站、预约取消、开始/停止二次确认和会话失效。
- 双 TCP 客户端测试通过：共享当前订单；并发停止恰好一个成功、另一个收到 `40903`；
  两端刷新最终状态；自动结束进入待支付后，充值和补支付结果、昵称、余额、工单及管理员
  回复可由另一端刷新。
- 关闭 WebEngine/scanner 的干净构建共 20 个测试全部通过；启用两项能力的构建中，排除
  需要桌面/Xvfb 的 WebEngine 导航测试后 20 个测试全部通过，扫码测试另行通过。
- 当前环境没有 `xvfb-run` 或可用桌面 DISPLAY，因此未执行 WebEngine SDK 替身导航回归，
  也未用真实 Tencent Key、在线瓦片或真实摄像头做人工验收；这些不由 offscreen 冒充。
