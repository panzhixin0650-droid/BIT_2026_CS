# 复杂版扩展参考

本目录保留项目早期的复杂设计，便于以后恢复计价、流水、工单、权限、设备管理和预测历史等能力。

## 文档

- [`cs-contract-extension-reference.md`](cs-contract-extension-reference.md)：复杂 C/S 消息、DTO、权限和可靠性候选设计。
- [`database-extension-reference.md`](database-extension-reference.md)：24 表数据库、事务、指标和运维候选设计。

## 使用方式

这些文档不是当前 Demo 的事实源，也不能整体复制回主线。扩展时只选择一个有消费者的能力，形成一组可审查的增量：

```text
确认需求 -> 更新当前契约/新建 V2 -> 增加编号迁移 -> 实现 -> fixture/测试
```

两份参考文档之间仍可能存在历史口径差异。每份文档开头列出了迁入前必须重新裁决的事项；最终决定应记录到 `docs/decisions/`。
