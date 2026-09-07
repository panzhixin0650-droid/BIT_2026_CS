# ADR-0010：扫一扫报修并入工单

- 状态：Accepted，已在任务分支实现，待合并。
- 日期：2026-09-07。
- 消费者：用户端扫一扫、我的工单；服务端系统管理员工单页。
- 受影响模块：client、shared/protocol、server、database、contracts。

用户明确要求参考保留的改进文档，实现扫一扫报修按钮，并允许报修合并到工单。
采用现有客服工单的提交去重、权限、分页、回复及 OPEN / IN_PROGRESS / RESOLVED 状态。
从扫一扫预填桩编号，用户选择故障类型、填写描述、确认后提交；不要求已有充电订单。
管理员在同一工单列表看到“报修”标识、桩编号和故障类型，用户通过我的工单查看处理结果。
没有模型配置也可完整使用报修；报修内容不经过 AI。

参考[旧接口第 12、16 节](../extension/cs-contract-extension-reference.md)及
[旧数据库 fault_reports](../extension/database-extension-reference.md)提取设备关联与人工处理需求。
旧方案使用独立故障表及五种状态，本次依用户选择以结构化设备信息区分同表工单。
旧方案的自动故障终止、免单、桩置 FAULT、设备重启、维修派单及故障历史仍为扩展候选。
提交或解决报修均不改变桩、订单、计费和账户状态，正在充电的用户需走已有结束充电流程。
工单处理继续仅允许系统管理员，不扩展站点管理员的数据范围。

## 兼容与迁移

沿用 support.ticket.create/list/detail，草稿和 DTO 增加可选 repair 对象，具体见
[工单 V1 契约](../../contracts/support-tickets-v1.md)。普通客服请求保持原有四字段。
新服务端支持 schema 1–4：schema 1 保留原业务，schema 2/3 保留普通客服工单，
报修需要 schema 4。旧服务端拒绝未知 repair 请求，不降级成丢失设备信息的普通单。
旧客户端可忽略响应中的 repair 对象；新客户端按结构化字段显示设备信息。

[迁移 004](../../database/migrations/004_repair_tickets.sql)仅在 schema 3 上为
support_tickets 增加可空 pile_code 外键、fault_type 及设备索引；历史记录保持普通客服单。
桩编号来自现有 charging_piles，外键阻止删除/改名已关联的桩，保留报修对象可追溯性。
停服备份后显式执行，测试只创建临时数据库，不升级用户现有运行数据库。

## 验证

覆盖协议合法/非法 repair 字段、SQLite 新迁移和历史记录兼容、内存与 SQLite 流程、
真实 TCP 提交/查询、管理员处理、用户隔离、无效桩号、冻结用户、幂等与业务状态保持。
UI 覆盖扫一扫入口、用户确认、草稿保留、断线后的不可变重试和处理结果显示。
