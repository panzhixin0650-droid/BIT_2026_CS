# 客服工单：兼容 V1 增量

此文件由 [ADR-0008](../docs/decisions/0008-support-ticket-desk.md) 授权，沿用
[V1 信封、错误码与 256 KiB 帧上限](overall-interface-v1.md)，不修改既有消息语义。
模型不是业务调用方；工单仅由用户确认后上传。用户身份来自 token，不接受客户端指定用户。

## 数据

工单草稿所有字段必填：

| 字段 | 类型 | 约束 |
| --- | --- | --- |
| `submissionId` | string | 小写、无花括号的 UUID；同一用户内唯一，显式重试沿用 |
| `title` | string | 1–80 字符，不得全为空白 |
| `summary` | string | 1–4000 字符，用户确认的正文；不得全为空白 |
| `sourceModel` | string | AI 起草时为模型名；手工填写为空；最长 120，仅字母数字及 `._:-` |

文本不接受除换行、回车、制表符以外的控制字符。所有字段按纯文本展示，不执行 HTML。
请求不得混入 `userId`、工单状态、管理员回复等额外字段。对话或模型输出均不是可信业务事实。

`SupportTicketDto` 包含草稿四个字段，另含：

| 字段 | 类型 | 含义 |
| --- | --- | --- |
| `ticketId` | positive integer | 服务端生成的工单 ID |
| `userId` | positive integer | token 对应用户 ID |
| `status` | string | `OPEN` 待处理、`IN_PROGRESS` 处理中、`RESOLVED` 已解决 |
| `reply` | string | 管理员回复；初始为空，最长 2000 字符 |
| `createdAt` / `updatedAt` | datetime | 服务端 UTC ISO 8601 时间 |

ID 使用与 V1 一致的 JSON 安全整数。状态和回复变更不表示任何充电、退款或账户操作已执行。

## 用户 TCP 消息

所有消息需要 token，返回 `40101` 时按既有规则重新登录；冻结用户返回 `40301`。

| type | 请求 `data` | 成功响应 `data` |
| --- | --- | --- |
| `support.ticket.create` | 工单草稿 | `{ticket: SupportTicketDto}` |
| `support.ticket.list` | `{beforeTicketId?: positive integer}` | `{items: SupportTicketDto[], hasMore: bool}` |
| `support.ticket.detail` | `{ticketId: positive integer}` | `{ticket: SupportTicketDto}` |

列表仅返回当前用户的记录，按 `ticketId` 倒序，每次最多 10 条；`hasMore=true` 时使用
本页最后一个 ID 作为下页游标，只返回比它小的 ID。不得通过额外 `userId` 参数查询他人。
10 条完整正文的最坏编码体积仍受现有帧上限约束，不扩大整个协议的大小限制。

首次创建状态固定为 `OPEN`、回复为空。相同用户、相同 `submissionId`、相同草稿
返回已有工单（包括管理员后来更新的状态/回复），不得创建第二条。UUID 与内容不一致
返回 `40001`，不覆盖原记录。`submissionId` 不是鉴权凭据，可由不同用户分别使用。

记录不存在或不是当前用户的记录均返回 `40401`。非法字段/格式返回 `40001`。
未应用数据库迁移或 Repository 不支持工单时返回 `50301`；内部存储失败返回 `50001`，
不暴露 SQL、数据库路径或凭据。旧服务端可能返回 `40001`，新客户端应提示检查工单支持。
失败和断线不得假装提交成功；无法确认时保留原提交编号，只允许用户显式重试或查询核对。

## 管理端与持久化

管理员通过本地 AdminFacade 查看工单、设置状态和回复；未登录返回 `40301`。
不提供用户可访问的管理员更新 TCP 消息。更新只接受 `ticketId/status/reply`，已处理
记录必须填写回复；合法状态可由管理员纠正，不改变工单原作者、标题、正文或创建时间。
管理员退出后清除工单访问授权。

仅新增[迁移 002](../database/migrations/002_support_tickets.sql) 的 `support_tickets` 表。
保留旧 `user_version=1` 的业务可用性，启用工单需显式升级到 2。完整对话、AI Key、
用户 token、手机号与实时位置不进入工单字段；用户应在提交预览中移除不必要的个人信息。

示例：[创建请求](examples/support-ticket-create.request.json)、
[创建/详情响应](examples/support-ticket-create.response.json)、
[列表响应](examples/support-ticket-list.response.json)。
