# 数据库迁移目录

`001_initial_demo.sql` 是当前五表 Demo 的首个结构迁移，从空 SQLite 数据库建立 `users`、`admins`、`charging_stations`、`charging_piles` 和 `charging_orders`，并设置 `PRAGMA user_version = 1`。

迁移只执行一次，不以“重复运行不报错”为目标。后续结构变化按 `002_<feature>.sql`、`003_<feature>.sql` 递增；`001_initial_demo.sql` 进入共享分支后不得覆写。扩展参考文档中的旧 DDL 不得整段复制到这里。

每个 SQLite 连接的 `foreign_keys` 设置都是独立的。迁移会为执行它的连接开启外键，服务端 Repository 连接仍必须在打开后再次执行 `PRAGMA foreign_keys = ON`。
