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
forms/           Qt Designer 文件
resources/       qrc、图标、默认头像
tests/           用户端单元测试
```

接口字段以 [`contracts/overall-interface-v1.md`](../contracts/overall-interface-v1.md) 为准，不从服务端实现反推。

本次只建立责任边界，不提前创建页面类或 CMake target；客户端负责人开始实现时自行补齐。
