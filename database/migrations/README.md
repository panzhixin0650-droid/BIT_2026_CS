# 数据库迁移目录

`001_initial_demo.sql` 建立五张核心表（schema 1）；`002_support_tickets.sql` 增加工单（schema 2）；
`003_admin_accounts.sql` 在其基础上升级 `admins`，增加站点授权和管理员审计（schema 3）。
原五表种子应在 003 之前执行。已有库先停服并备份，按顺序显式升级，见[部署说明](../README.md)。

迁移只执行一次，缺少前置迁移、重复执行和未知结构均应拒绝。后续迁移从 `004_<feature>.sql`
继续递增；已经合并的编号迁移不得覆写。扩展参考文档的旧 DDL 不得整段复制到这里。

每个 SQLite 连接的 `foreign_keys` 设置都是独立的。迁移会为执行它的连接开启外键，服务端 Repository 连接仍必须在打开后再次执行 `PRAGMA foreign_keys = ON`。
