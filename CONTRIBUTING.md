# 协作开发约定

## 1. 分支和提交

- `main` 保持可配置、契约一致、数据库可初始化；
- 从最新 `main` 创建短期分支，不建立长期 `client`/`server` 大分支；
- 推荐分支：`client/<feature>`、`server/<feature>`、`web/<feature>`、`db/<change>`、`contract/<change>`；
- 一个提交只做一个可解释的变化，不提交 `build/`、Qt Creator 用户配置、运行时数据库或密钥。

## 2. 目录所有权

| 变化 | 主要目录 | 需要同步确认 |
| --- | --- | --- |
| 用户页面/API 适配 | `client/` | 改协议时通知服务端 |
| TCP/业务/管理端 | `server/` | 改协议时通知客户端，改表时通知数据库负责人 |
| DTO/帧编解码 | `shared/protocol/` | 客户端和服务端共同确认 |
| 消息或字段语义 | `contracts/` | 客户端和服务端共同确认 |
| 表结构和种子 | `database/` | 服务端负责人确认 |
| ECharts 页面 | `web/` | 快照格式变化时通知服务端 |

不要顺手重构其他成员负责的应用目录。真正的跨模块变化应通过 `contracts/`、`database/` 或 `shared/protocol/` 明确暴露。

## 3. 契约优先

增加一个跨模块能力时，在同一个变更中完成：

1. 修改 `contracts/overall-interface-v1.md` 或新建 V2 契约；
2. 更新 `contracts/examples/` fixture；
3. 必要时新增数据库迁移；
4. 客户端和服务端分别实现；
5. 增加最小失败分支测试。

V1 可以增加新消息或客户端可忽略的字段。重命名字段、改变类型/单位、改变已有状态含义属于破坏性变化，应建立 V2。

## 4. 数据库变化

- 数据库负责人首次实现时创建 `database/migrations/001_initial_demo.sql`；进入共享分支后不要为了新功能反复改写它；
- 新结构使用 `002_<name>.sql`、`003_<name>.sql` 递增；
- 所有用户输入使用参数绑定，连接开启 `PRAGMA foreign_keys=ON`；
- 预生成 `.db` 只是样例，编号 SQL 才是事实源；
- 禁止把 `docs/extension/` 中的旧 DDL 整段执行到 Demo 数据库。

## 5. 合并前最少检查

```bash
git diff --check
git status --short
```

模块出现实际 target、迁移或测试后，再由该负责人把对应命令补到本节。无法执行某项检查时，在 PR 中写明原因和未验证风险。
