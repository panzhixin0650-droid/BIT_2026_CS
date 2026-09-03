# 数据库责任区

本目录提供当前课程 Demo 的 SQLite 五表实现。编号迁移是结构事实源，演示种子用于本地开发和联调；运行时数据库始终在仓库外或被忽略的 `build/` 下生成，不提交二进制数据库。

## 目录

- `migrations/001_initial_demo.sql`：从空库建立五张业务表、必要索引和约束；
- `seeds/demo.sql`：可重复执行的课程演示数据；
- `tests/`：独立迁移、播种、完整性、订单事务和失败分支验证；
- `sample/`：确有联调需要时才提交经过检查的样例说明或数据。

字段、单位、状态和事务边界以 [`docs/design/demo-database-design.md`](../docs/design/demo-database-design.md) 为准。开始实现后，编号 SQL 才成为数据库结构事实源。

复杂参考文档中的 24 表 DDL 不能直接叠加到五表 Demo 库；应按功能拆成迁移并处理同名表字段差异。

## 独立初始化

前置条件是 SQLite 3.37.x。Ubuntu 22.04 可安装：

```bash
sudo apt install -y sqlite3
```

在仓库根目录执行：

```bash
mkdir -p build/database
demo_database=build/database/demo.db
sqlite3 -batch -bail "$demo_database" < database/migrations/001_initial_demo.sql
sqlite3 -batch -bail "$demo_database" < database/seeds/demo.sql
sqlite3 -batch -bail "$demo_database" < database/tests/verify_demo.sql
```

最后一条命令输出 `database verification: OK` 即表示迁移、种子、完整性、外键和核心场景一致。迁移只对新库执行一次；`demo.sql` 可重复执行且不会重复插入固定演示数据。需要重新得到标准演示状态时，应在 `build/` 下创建一个新数据库，而不是改写或删除已经共享的迁移。

一键验证全部成功和失败分支：

```bash
database/tests/run.sh
```

## 服务端接入边界

- 服务端使用 Qt `QSQLITE` 打开迁移生成的数据库；每个新连接必须执行 `PRAGMA foreign_keys = ON` 并确认结果为 `1`。
- Repository 是唯一 SQL 入口，所有外部输入使用 `QSqlQuery::prepare()` 和 `bindValue()`；UI、TCP Gateway、Web 和 Mock 不直接访问数据库。
- `PRAGMA user_version` 当前为 `1`，可用于服务端启动时拒绝未知结构版本；不要把 SQLite 路径或 SQL 错误暴露到 TCP 响应。
- 预约、开始、停止结算和补支付仍由 `ApplicationService` 按 V1 契约编排事务。部分唯一索引和检查约束只是防止错误写入，不能替代业务错误码判断。
- 金额、能量和时间分别使用整数分、整数 Wh 和 UTC ISO 8601；营收查询按 `paid_at` 转换到 `Asia/Shanghai` 业务日。
- `seeds/demo.sql` 仅用于开发/演示。服务端测试需要空状态时只执行迁移，不执行种子。
