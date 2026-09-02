# 跨模块契约

[`overall-interface-v1.md`](overall-interface-v1.md) 是当前用户端、服务端、管理端、Web 和 Mock 的唯一语义契约。

`examples/` 保存可直接用于客户端 Mock、服务端协议测试和 Web 开发的固定 JSON。fixture 必须遵守契约，但 fixture 本身不能反过来修改业务语义。

变更规则：

- 增加兼容消息或字段：更新 V1 文档和相应 fixture；
- 改名、改类型、改单位或改变已有状态含义：新建 V2；
- 不把 C++ 类布局、SQL 文本和页面样式写成跨模块契约。
