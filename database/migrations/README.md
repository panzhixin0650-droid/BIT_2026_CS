# 数据库迁移目录

`001_initial_demo.sql` 是五张核心业务表的首个结构迁移。`002_admin_accounts.sql` 兼容升级 `admins`，增加 `admin_station_scopes` 和 `admin_audit_logs`，并把 `PRAGMA user_version` 升到 2。

迁移只执行一次，不以“重复运行不报错”为目标。后续结构变化继续按 `003_<feature>.sql`、`004_<feature>.sql` 递增；已经合并的编号迁移不得覆写。扩展参考文档中的旧 DDL 不得整段复制到这里。

每个 SQLite 连接的 `foreign_keys` 设置都是独立的。迁移会为执行它的连接开启外键，服务端 Repository 连接仍必须在打开后再次执行 `PRAGMA foreign_keys = ON`。
