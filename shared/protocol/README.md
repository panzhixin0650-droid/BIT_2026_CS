# 共享协议实现

客户端和服务端共同编译的最小 C++17/Qt 6 Core 协议库，实现：

- 4 字节无符号大端长度帧的编码、增量解码和致命格式错误识别；
- V1 请求/响应信封的 JSON 转换与基本字段类型检查；
- V1 TCP 消息名、错误码和协议限制常量；
- 首轮用户链路需要的 `UserDto`、`StationDto`、`PileDto`、`OrderDto` 及 JSON 转换；
- 基于 `contracts/examples/` 的纯协议测试。

本库不包含 Socket、Session、Repository、订单状态机、UI 或业务提示。语义始终以 [`../../contracts/overall-interface-v1.md`](../../contracts/overall-interface-v1.md) 为准。

## 构建和测试

```bash
cmake -S shared/protocol -B build/protocol -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build/protocol
ctest --test-dir build/protocol --output-on-failure
```

库本身只依赖 `Qt6::Core`；启用测试时额外依赖 `Qt6::Test`。
