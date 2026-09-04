# Qt 用户端

本目录由用户端负责人独立维护，目标程序为 `user-client`。

## 边界

- 页面和 Controller 只依赖 `IChargingApi`；
- 开发期使用 `MockChargingApi`，联调时切到 `TcpChargingApi`；
- 用户端功能代码不实现 TCP、分帧、重连等通信细节；通信负责人后续提供
  `TcpChargingApi` 并在应用装配处替换 Mock；
- 不包含 SQL，不引用 `server/` 头文件，不计算权威金额或修改权威订单状态；
- 头像、地图和扫码属于客户端本地适配能力。

推荐逐步建立：

```text
src/ui/          QWidget、Controller/ViewModel
src/api/         IChargingApi、MockChargingApi、TcpChargingApi
src/local/       头像等仅保存在客户端本机的适配能力
src/model/       页面显示模型
resources/       qrc、图标、默认头像
tests/           用户端单元测试
```

接口字段以 [`contracts/overall-interface-v1.md`](../contracts/overall-interface-v1.md) 为准，不从服务端实现反推。

## 界面实现约定

客户端界面全部使用 C++ 代码构造，不创建或引用 Qt Designer `.ui` 文件。CMake 明确关闭 `AUTOUIC`，后续页面继续遵守相同约定。

## 当前实现

当前已提供：

- 纯 C++ 构造的 Qt Widgets 应用窗口和手机号登录页；
- typed `IChargingApi` 登录、退出、个人资料和钱包充值边界；
- 持有开发期登录态的 `MockChargingApi`；
- 复用共享 V1 长度帧、信封和 DTO 的 `TcpChargingApi`，支持 token、请求匹配、
  5 秒默认超时以及断线失败收敛；
- 手机号格式校验、提交 loading、中文错误提示；
- 已有用户登录和新手机号自动注册后的页面切换；
- 位于窗口底部的“充电、订单、扫一扫、客服助理、我的”5 个主入口；
- “我的”页面的资料刷新、昵称修改、余额充值和退出登录；
- 灰色默认头像，以及本地图片导入、归一化保存和圆形显示；
- “充电”首页的演示定位、区域/关键词查询和充电站列表；
- 充电站详情及站内电桩类型、功率和状态展示；
- 当前订单检查、闲置桩预约、首页预约卡片和取消预约闭环；
- “订单”页的历史列表、进行中状态高亮、完整详情和预约取消入口；
- “扫一扫”页的开发环境二维码内容输入，以及直接充电/预约转充电流程；
- 充电进度刷新、结束充电二次确认、自动结算和余额不足后的补支付闭环；
- 客户端本地 `IMapService`、Mock 地址解析、位置选择，以及可选的腾讯地图
  WebEngine 驾车/步行导航页面；
- API、Mock、界面与共享协议测试。

当前可执行程序默认装配 `MockChargingApi`，用于离线开发页面；传入 `--api tcp`
即可切换到真实服务端。Mock 数据仅保存在当前进程内，退出程序后新注册用户不会
保留；它不会连接数据库。两种实现均通过 `IChargingApi` 向页面返回相同的 typed
结果，页面和 Controller 不直接处理 TCP 信封或 token。

构建和测试：

```bash
cmake -S client -B build/client -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build/client
ctest --test-dir build/client --output-on-failure
```

## 运行模式与联调入口

> **默认模式：** 不填写任何运行参数时，充电业务使用 `MockChargingApi`，地图使用
> `MockMapService`。`--api` 和 `--map` 是相互独立的选项，可以只切换其中一个。

| 目的 | Command line arguments |
| --- | --- |
| 日常离线开发（默认） | 留空，或 `--api mock --map mock` |
| 仅联调 TCP，地图仍用 Mock | `--api tcp --host 127.0.0.1 --port 45678 --map mock` |
| 仅联调腾讯地图，业务仍用 Mock | `--api mock --map tencent` |
| 同时联调 TCP 和腾讯地图 | `--api tcp --host 127.0.0.1 --port 45678 --map tencent` |

日常页面开发默认保持 Mock 模式，不连接服务端或数据库：

```bash
./build/client/user-client
```

上面的命令等同于显式传入 `--api mock --map mock`。Mock 数据只存在于当前客户端
进程，适合其他成员继续独立开发和调试界面。

Qt Creator 中打开 `Projects → Run`，将 `Command line arguments` 留空即可使用
默认的业务 Mock 和地图 Mock；也可以填写 `--api mock --map mock` 明确指定。

需要进行真实 TCP 联调时，从仓库根目录先启动已经初始化好 Demo 数据库的服务端：

```bash
./build/server/server-app --database build/database/demo.db
```

再在另一个终端显式选择 TCP 模式启动用户端：

```bash
./build/client/user-client --api tcp --host 127.0.0.1 --port 45678
```

`--api tcp` 是联调测试入口；该模式只请求真实服务端，不会在接口失败时回退到
Mock。数据库初始化步骤见 [`database/README.md`](../database/README.md)，服务端
启动参数见 [`server/README.md`](../server/README.md)。

Qt Creator 中联调 TCP 时，先在终端启动上述服务端，再打开 `Projects → Run`，将
`Command line arguments` 设置为：

```text
--api tcp --host 127.0.0.1 --port 45678 --map mock
```

切回 Mock 时清空参数，或改回 `--api mock --map mock`。这些选项只改变客户端运行时
装配，不修改源码、数据库或 Git 文件。

默认请求超时为 5000 毫秒，可用 `--timeout-ms <毫秒>` 调整。连接失败、断线和
超时统一返回 `50301 SERVICE_UNAVAILABLE`；断线和 `40101 INVALID_SESSION` 会
清除客户端 token，用户需重新登录。当前服务端实际开放到哪些 TCP 消息，以
[`server/服务端网络接口.md`](../server/服务端网络接口.md) 为准；尚未开放的订单消息
会返回服务端的业务失败，不会回退到 Mock。

### 地图模式与腾讯地图依赖

地图默认使用 Mock，普通客户端构建不强制 Qt WebEngine。需要构建可选的腾讯地图
适配器和内嵌路线页面时，客户端开发者需要安装完整的 Qt WebEngine 开发及运行工具：

> **额外安装项（启用真实腾讯地图前必须完成）：** 以下三个 WebEngine 包不是基础
> Mock 开发依赖，但构建 `--map tencent` 支持时缺一不可。

```bash
sudo apt update
sudo apt install -y \
  qt6-webengine-dev qt6-webengine-dev-tools libqt6webenginecore6-bin
```

安装后使用下面的开关重新运行 CMake 配置；如果 Qt Creator 已打开，在项目的 CMake
配置中加入 `CHARGING_CLIENT_ENABLE_WEBENGINE:BOOL=ON` 并重新配置，使 Kit 能发现
`Qt6::WebEngineWidgets`：

```bash
cmake -S client -B build/client -G Ninja -DCMAKE_BUILD_TYPE=Debug \
  -DCHARGING_CLIENT_ENABLE_WEBENGINE=ON
cmake --build build/client
```

日常开发保持充电业务和地图均为 Mock：

```bash
./build/client/user-client --api mock --map mock
```

在已经启用 WebEngine 的构建中，需要单独联调腾讯地址解析和内嵌导航时，可以继续
使用 Mock 充电业务，只切换地图适配器：

```bash
export TENCENT_MAP_KEY='你的本地腾讯地图Key'
./build/client/user-client --api mock --map tencent
```

`TENCENT_MAP_KEY` 是腾讯位置服务控制台创建的开发 Key。Qt Creator 中打开
`Projects → Run → Run Environment` 并新增一行：变量名填写
`TENCENT_MAP_KEY`，Value 只填写 Key 本身，不要带引号，也不要重复填写
`TENCENT_MAP_KEY=`；Command line arguments 设为 `--api mock --map tencent`。
该 Key 需要在腾讯位置服务控制台同时启用 WebService API 和 JavaScript API GL：
地址解析与步行路线由 WebService 提供，步行折线由 JavaScript API GL 在内嵌页面中
绘制。当前客户端 Demo 不保存用于服务端签名的 SK；如果 Key 强制签名校验，应改用
未开启签名校验且限制好权限/配额的开发 Key，不能把 SK 嵌入客户端。切回 Mock 时
删除本地 Key 环境变量并将参数改回
`--api mock --map mock` 或全部清空。真实模式不会在失败时偷偷返回 Mock 坐标；缺少
Key 时程序会明确拒绝启动，地址解析失败会显示腾讯的非敏感状态码，页面加载失败也
会显示中文错误。腾讯地图 Key 禁止写入源码、仓库配置、接口 fixture 或 Git 管理的
Qt Creator 文件。

启动后输入 11 位数字手机号：

- `13800000001`：登录契约中的固定演示用户，昵称为“演示用户0001”；
- 任意其他 11 位数字：在当前 Mock 进程内自动注册，昵称为“用户+手机号后4位”；
- 少于 11 位：页面直接显示格式错误，不发送 API 请求。

Linux 下程序会在创建 `QApplication` 前检查 Qt 输入法插件。如果桌面配置为
Fcitx/Fcitx5、当前 Qt 6 安装缺少 Fcitx 插件但提供 IBus 插件，程序只在自身进程内
回退到 Fcitx5 的 IBus 兼容接口，以便昵称输入框正常切换中文；不会修改桌面或用户的
全局输入法配置。Qt 已安装 Fcitx 插件或用户明确选择其他输入法时，程序保持原配置。

登录成功后进入用户端主界面。当前“充电”、“订单”、“扫一扫”和“我的”页面
已经可用，“客服助理”仍显示后续功能提示。

底部 5 个主入口按相同宽度铺满窗口，不使用靠左堆叠或横向滚动；窗口宽度变化时
由 `QTabBar` 自动重新分配每个入口的宽度。

“充电”页面将“当前位置”和“查找充电站”分开显示，避免把地址定位误认为站点
筛选。使用方法：

- 首页采用单一纵向滚动区域；欢迎信息、当前订单、位置查询和充电站列表一起滚动，
  有进行中订单时不会再把充电站列表压缩成很小的嵌套滚动区域；
- 默认使用演示坐标 `123.4200, 41.7000` 计算距离；也可以从和平区、浑南区
  快捷位置中选择，或手动输入包含城市名称的完整地址并点击“确定位置”。用户开始
  手动编辑地址后，快捷位置会自动切换到“手动输入地址”，不再保留冲突的旧选项；
- 当前 `MockMapService` 只解析演示位置及包含“和平”或“浑南”的地址，不把其他
  任意地址伪装成真实坐标。成功后统一得到经纬度并重新调用 `station.list`；
  不支持或解析失败时保留上一次有效位置，页面不会不可用；
- 取消“使用当前选定位置计算距离”后执行无坐标查询，此时页面显示“距离待定位”；
- 位置只用于当前客户端会话和本次站点请求，不写数据库，也不进入 TCP 用户资料；
- “站名或地址关键词”支持模糊查询，例如输入“和平”可以找到和平演示充电站；
- 区域是可选的精确筛选，需输入完整名称，例如“和平区”；它不是位置输入框；
- 列表展示空闲桩数、站点当前价格、距离和未来一小时拥堵预测；
- 点击充电站卡片任意位置可查看站内全部电桩，卡片也支持键盘 Enter/Space；只有
  `IDLE` 桩的“预约”按钮可用；
- 预约前客户端刷新当前订单，服务端/Mock 仍会最终校验当前订单和桩状态；
- 预约成功后首页优先展示当前预约，可点击“取消预约”；页面只使用 API 返回的
  `OrderDto` 和重新获取的站点数据，不自行修改订单或桩状态；
- 当前 Demo 预约没有自动过期、倒计时或违约次数，客户端不擅自增加这些状态；
- 充电中的当前订单可刷新进度；开发期 Mock 每次刷新推进一分钟演示数据，页面只
  展示 API 返回的充电量、时长和预估金额；该“推进一分钟”只属于 Mock 演示，
  切换 `TcpChargingApi` 后刷新只展示服务端/设备返回的实际读数；
- 点击“结束充电”必须二次确认。Mock 模拟服务端生成最终账单、释放电桩并尝试
  钱包扣款：余额足够时直接完成，余额不足时订单进入待支付；
- 当前 Mock 与数据库演示种子保持一致，只返回启用的浑南站和和平站。后续切换
  `TcpChargingApi` 后，由服务端执行筛选、距离排序、实时聚合与推荐。
- 充电站卡片和详情页均提供“导航”；起点默认使用当前选定位置且允许修改，出行方式
  按 V1 契约只提供驾车和步行。未修改的默认起点直接复用首页已有坐标，不再解析
  “演示位置”这类展示文字；导航页临时修改的真实地址会重新解析，但不会覆盖首页的
  当前选定位置。Mock 显示静态路线摘要；使用 `--map tencent` 时，驾车加载腾讯
  路线页面，步行调用腾讯步行路线 WebService 并在 `QWebEngineView` 中绘制返回的
  路线折线。公共交通尚未列入 V1 `RouteMode`，因此当前不增加契约外的出行方式。

“扫一扫”页面使用方法：

- 当前 Ubuntu 虚拟机开发入口允许输入二维码解析后的充电桩编号，可使用快捷按钮
  `PILE-A-01` 或 `PILE-B-02`；这部分是独立的开发适配入口，不冒充真实摄像头；
- 开始前先通过 `order.current` 检查当前订单。没有当前订单时扫描闲置桩可直接开始；
  扫描本人已预约的同一充电桩时复用原预约订单，不重复创建订单；
- 已有其他预约、充电中或待支付订单时会中止本次操作，并引导到相应页面；
- 后续真实摄像头只需替换扫码内容来源，启动充电仍复用现有 Controller 与
  `IChargingApi` 流程；扫码适配器将解析出的 `pileCode` 交给
  `ScanController::submitPileCode()`，与手工输入共用同一套前置检查和启动逻辑。

“订单”页面使用方法：

- 进入底部“订单”时通过 `order.list` 刷新当前用户全部订单，并按创建时间倒序展示；
- 预约中、充电中和待支付订单使用高亮卡片，历史订单使用普通卡片；
- 点击订单卡片任意位置展示接口返回的时间、电量、订单价格快照和金额，卡片也
  支持键盘 Enter/Space；可空字段不由客户端自行构造；
- 卡片内已经显示“点击卡片查看详情”，不再额外显示跟随鼠标的重复工具提示；
- 预约中、充电中和待支付卡片使用蓝色业务高亮；鼠标悬停/键盘焦点使用中性灰色
  背景和边框，两者视觉含义明确区分；
- 预约中订单可在详情页取消，成功后重新调用 `order.list` 获取最终状态；
- 首页预约卡片和预约订单详情均提供“前往扫码充电”，跳转后自动填入预约桩编号；
  用户仍需到桩后确认开始，客户端不会仅因点击跳转就修改订单状态；
- 充电中订单可在详情页点击“结束充电”，二次确认后调用 `order.stop`；成功后
  重新获取订单列表，余额足够显示已完成，余额不足显示待支付；
- 充电中订单详情可调用 `order.progress` 刷新时长、电量和预估金额；详情页只用
  响应中的 `OrderDto` 更新展示，不在页面本地累计读数；
- 待支付订单详情显示“立即结算”和“前往充值”。补支付请求只提交订单标识，金额、
  扣款和最终状态均来自 API；余额不足时订单保持待支付；
- 用户存在待支付订单时再次预约，客户端在当前订单检查后直接切换到“订单”页，
  不再停留在充电站详情；服务端仍负责最终拒绝重复活跃订单；
- “前往充值”切换到“我的”，充值成功后返回订单详情可再次结算；
- 当前 Mock 为固定演示用户提供与数据库种子一致的已完成历史订单，新预约和取消
  仍只保存在本次进程中；
- 充电中的刷新和结束操作放在“充电”首页的当前订单卡片，订单页负责历史详情、
  预约取消和待支付结算。

“我的”页面使用方法：

- 进入页面时自动通过 API 刷新手机号、昵称和余额；
- 输入 1 到 32 个字符的昵称并点击“保存”，成功响应后使用返回的 `UserDto`
  同步“我的”资料和“充电”首页欢迎语，不使用输入框内容提前修改其他页面；
- 可选择 10、20、50、100 元，或手动输入 0.01 到 10000 元充值；
- 页面余额只使用充值响应中的 `balanceCents`，不在 UI 中直接累加；
- 点击“更换头像”可导入本地图片，图片会缩放到最大 512×512 并保存为 PNG；
- 点击“退出登录”会清除 Mock token 并返回登录页。

头像不经过 TCP。Linux 下使用 `QStandardPaths::AppDataLocation` 保存图片，并由 `QSettings` 保存每个用户对应的相对路径；这些运行时文件位于仓库外，不会进入 Git。
