# Qt 用户端

本目录由用户端负责人独立维护，目标程序为 `user-client`。

## 边界

- 页面和 Controller 只依赖 `IChargingApi`；
- 开发期使用 `MockChargingApi`，联调时切到 `TcpChargingApi`；
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
- 手机号格式校验、提交 loading、中文错误提示；
- 已有用户登录和新手机号自动注册后的页面切换；
- 位于窗口底部的“充电、订单、扫一扫、客服助理、我的”5 个主入口；
- “我的”页面的资料刷新、昵称修改、余额充值和退出登录；
- 灰色默认头像，以及本地图片导入、归一化保存和圆形显示；
- API、Mock、界面与共享协议测试。

当前可执行程序默认装配 `MockChargingApi`，用于在 TCP 客户端接入前独立开发页面。Mock 数据仅保存在当前进程内，退出程序后新注册用户不会保留；它不会连接数据库，也不代表真实服务端已经完成调用。后续接入 `TcpChargingApi` 时，页面和 Controller 继续只依赖 `IChargingApi`。

构建和测试：

```bash
cmake -S client -B build/client -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build/client
ctest --test-dir build/client --output-on-failure
```

运行：

```bash
./build/client/user-client
```

启动后输入 11 位数字手机号：

- `13800000001`：登录契约中的固定演示用户，昵称为“演示用户0001”；
- 任意其他 11 位数字：在当前 Mock 进程内自动注册，昵称为“用户+手机号后4位”；
- 少于 11 位：页面直接显示格式错误，不发送 API 请求。

登录成功后进入用户端主界面。当前“我的”页面已经可用，其余 4 个入口暂时显示后续功能提示。

“我的”页面使用方法：

- 进入页面时自动通过 API 刷新手机号、昵称和余额；
- 输入 1 到 32 个字符的昵称并点击“保存”，成功响应后刷新资料；
- 可选择 10、20、50、100 元，或手动输入 0.01 到 10000 元充值；
- 页面余额只使用充值响应中的 `balanceCents`，不在 UI 中直接累加；
- 点击“更换头像”可导入本地图片，图片会缩放到最大 512×512 并保存为 PNG；
- 点击“退出登录”会清除 Mock token 并返回登录页。

头像不经过 TCP。Linux 下使用 `QStandardPaths::AppDataLocation` 保存图片，并由 `QSettings` 保存每个用户对应的相对路径；这些运行时文件位于仓库外，不会进入 Git。
