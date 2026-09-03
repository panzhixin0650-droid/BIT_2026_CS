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
src/model/       页面显示模型
resources/       qrc、图标、默认头像
tests/           用户端单元测试
```

接口字段以 [`contracts/overall-interface-v1.md`](../contracts/overall-interface-v1.md) 为准，不从服务端实现反推。

## 界面实现约定

客户端界面全部使用 C++ 代码构造，不创建或引用 Qt Designer `.ui` 文件。CMake 明确关闭 `AUTOUIC`，后续页面继续遵守相同约定。

## 当前实现

当前已提供最小 Qt Widgets 应用骨架、独立 CMake 构建入口和窗口构造测试。业务页面、`IChargingApi`、Mock 与 TCP 实现将在后续小任务中逐步加入。

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
