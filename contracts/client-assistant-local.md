# 客户端本地智能助理边界（第一版）

本文件是 [ADR 0005](../docs/decisions/0005-client-rag-assistant.md) 的外部接口说明，
不是 V1 TCP 消息，不增加 `support.*`，不包含用户 token 或数据库写操作。

以上是普通助理（Luna）的边界，保持兼容。后续独立模拟坐席和用户确认的工单扩展见
[ADR 0008](../docs/decisions/0008-support-ticket-desk.md) 与
[工单 V1](support-tickets-v1.md)：只有确认提交走业务 TCP，模型适配器仍无业务权限。
可选 `supportModel` 默认 `gpt-5.6-sol`，模拟坐席与摘要复用普通助理的 `baseUrl`、Key
和 Responses 请求格式，只切换模型名称与任务提示词。坐席在没有检索命中时也可澄清问题，
不得编造知识。普通助理无命中不联网的规则不变。

## 输入与输出

- 输入：1–1200 字符的问题、最近最多四组成功问答、本地/模型模式选择。
- 检索：随包 JSON 的标题、关键词、内容；最多三条；没有相关知识则明确表示
  知识不足，不调用模型臆测项目功能。简短追问可结合上次成功问题检索。
- 输出：纯文本回答、检索来源（ID/标题/内容/仓库路径）、本地或 AI 标记，
  明确的完成、失败或取消状态。来源是“本次检索参考”，不代表模型已逐句验证。
- 会话仅存在内存；失败/取消的回答不进入后续上下文。新对话及退出登录取消
  在途请求并清空会话，晚到事件不得进入新会话。

## 外部 HTTPS

本地配置包含 `baseUrl`、`apiKey`、`model`、`timeoutMs`、`maxOutputTokens`。
Key 不进入 Git、命令行参数、日志或知识库。URL 必须是 HTTPS，不允许用户信息、
查询参数、片段或重定向。`--assistant-config` 仅接受配置文件路径。

请求 `POST <baseUrl>/responses`，带 Bearer Key 和 JSON Content-Type；
配置若已以 `/responses` 结尾则不重复拼接。
发送 `model`、`instructions`、`input`、`stream: true`、`store: false`、
`max_output_tokens`。不发送工具，不让模型获得业务 API 权限。
`store: false` 是协议请求，不承诺第三方供应商不保留服务日志。

解析 SSE 的 `response.output_text.delta`、`response.completed`、
`response.failed`、`response.incomplete` 和 `error`；也接受 completed 状态的
非流式 Responses JSON。不完整流、空回答、异常结构均不得标成成功。
超时/HTTP 错误展示中文提示且不自动重试；用户可显式重试或切本地模式。

AI HTTP 默认对已配置地址直连，不使用桌面自动代理发现，不更改全局／系统代理。
网络初始化和流式处理不得阻塞窗口事件；等待时显示活动指示、秒数及停止入口。
超时包含初始化等待，取消后尚未派发的请求不发送，晚到事件不得恢复旧会话。
对供应商已经收到的请求，取消不承诺撤回生成或免除费用；也不自动重试。
文字片段可合并刷新，但成功结果必须保留完整回答。

只发送通用聊天与检索知识；手机号和形似 `sk-...` 的内容尽力脱敏，不能代替用户
避免输入敏感信息。限制问题、历史、总响应和超时，串行执行一个请求。

示例见 [`examples/assistant-responses.local.json`](examples/assistant-responses.local.json)。
接口依据：[供应商说明](https://docs.right.codes/docs/rc_extension/curl)、
[OpenAI 流式响应文档](https://developers.openai.com/api/docs/guides/streaming-responses)。
