# 文档导航与效力

## 当前 Demo 基线

| 文档 | 作用 |
| --- | --- |
| [`requirements/project-spec.doc`](requirements/project-spec.doc) | 课程验收的原始项目说明书 |
| [`../contracts/overall-interface-v1.md`](../contracts/overall-interface-v1.md) | 当前跨模块接口、DTO、状态和错误码 |
| [`design/demo-database-design.md`](design/demo-database-design.md) | 当前五表数据库设计与字段说明 |
| [`design/architecture/demo-architecture.png`](design/architecture/demo-architecture.png) | 当前架构图 PNG |
| [`design/architecture/demo-architecture.svg`](design/architecture/demo-architecture.svg) | 可编辑架构图 SVG |

## 未来扩展参考

[`extension/`](extension/README.md) 中的两份复杂文档是未来需求池和设计素材，不是当前接口或数据库事实源。只有在新增功能已经被确认、写出增量迁移和兼容方案后，相关小节才会转化为新的基线文档。

## 开发与协作指南

| 文档 | 作用 |
| --- | --- |
| [`guides/development-environment.md`](guides/development-environment.md) | Ubuntu 22.04.3、Qt 6.2.4、Creator 6.0.2、按角色安装和环境自检 |
| [`guides/parallel-development.md`](guides/parallel-development.md) | 各模块的独立开工条件、共享边界与联调检查点 |
| [`guides/github-collaboration.md`](guides/github-collaboration.md) | Ubuntu 安装、GitHub 账号与 SSH、短期分支、PR、冲突处理 |
| [`../CONTRIBUTING.md`](../CONTRIBUTING.md) | 必须遵守的精简协作规则 |

## 已接受决策

| 文档 | 作用 |
| --- | --- |
| [`decisions/0001-demo-first.md`](decisions/0001-demo-first.md) | Demo 优先，复杂能力按需迁入 |
| [`decisions/0002-development-environment.md`](decisions/0002-development-environment.md) | 冻结课程 Demo 的参考开发环境 |
| [`decisions/0003-client-transit-navigation.md`](decisions/0003-client-transit-navigation.md) | 用户端公共交通导航、本地地图边界与兼容方式 |
| [`decisions/0004-client-cycling-navigation.md`](decisions/0004-client-cycling-navigation.md) | 用户端自行车骑行导航与腾讯地图适配 |

## 规则

- 当前实现不得因为扩展文档出现某张表或某个接口就提前增加它。
- 基线发生兼容性增加时，在现有契约中更新并补 fixture。
- 破坏性网络变更新建 V2 契约；数据库结构变化增加编号迁移文件。
- 数据库开始实现后，编号迁移 SQL 是结构事实源；网络语义始终以当前接口契约为准。文档与实现冲突时应在同一变更中修正。
