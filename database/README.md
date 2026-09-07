# 数据库责任区

本目录提供当前课程 Demo 的 SQLite 五表实现。编号迁移是结构事实源，演示种子用于本地开发和联调；运行时数据库始终在仓库外或被忽略的 `build/` 下生成，不提交二进制数据库。

2026-09-07 经用户授权新增可选的客服工单扩展（schema 2），只追加 `support_tickets`，
不重写五表基线或种子。未执行扩展迁移的 schema 1 仍可运行原有业务。

## 目录

- `migrations/001_initial_demo.sql`：从空库建立五张业务表、必要索引和约束；
- `migrations/002_support_tickets.sql`：从 schema 1 增量启用客服工单；
- `seeds/demo.sql`：可重复执行的课程演示数据；
- `seeds/expansion_20_20_100_200.sql`：在基础演示数据上追加 20 个用户、20 个充电站、100 个电桩和 200 个订单；
- `tests/`：独立迁移、播种、完整性、订单事务和失败分支验证；
- `sample/`：确有联调需要时才提交经过检查的样例说明或数据。

字段、单位、状态和事务边界以 [`docs/design/demo-database-design.md`](../docs/design/demo-database-design.md) 为准。开始实现后，编号 SQL 才成为数据库结构事实源。

复杂参考文档中的 24 表 DDL 不能直接叠加到五表 Demo 库；应按功能拆成迁移并处理同名表字段差异。

## 启用客服工单扩展 schema 2

先完成下文的 schema 1 初始化及验证，或使用已有 schema 1 数据库。
**停止服务端，备份实际数据库，再执行一次迁移**。以下路径以仓库默认路径为例，
如果启动时指定了其他 `--database`，必须对同一个文件操作，不要另建空库：

```bash
(
  set -e
  test -f build/database/demo.db
  test "$(sqlite3 build/database/demo.db 'PRAGMA user_version;')" = 1
  # 每次创建独立备份目录，不覆盖已有备份；任一步失败则停止。
  support_backup_dir="$(mktemp -d build/database/support-backup.XXXXXX)"
  sqlite3 build/database/demo.db ".backup '$support_backup_dir/demo.db'"
  sqlite3 -batch -bail build/database/demo.db < database/migrations/002_support_tickets.sql
  sqlite3 build/database/demo.db 'PRAGMA user_version; PRAGMA integrity_check; PRAGMA foreign_key_check;'
  # 应输出 2、ok，且无外键错误。
  printf '备份保存在：%s/demo.db\n' "$support_backup_dir"
)
```

迁移检查起始版本、使用事务、保留旧 ID 和数据。重复应用或向未知版本应用会失败，
不要重跑 001、修改 `user_version` 绕过检查或删除旧库。服务端不自动执行迁移。
新的服务端接受 schema 1 / 2；schema 1 仅在工单接口返回需要升级的提示。
回退到只支持 schema 1 的旧服务端前需恢复备份，但备份之后的新数据不会随之保留；
不要在仍有业务写入时直接覆盖数据库。

工单字段、权限、去重与状态见 [工单契约](../contracts/support-tickets-v1.md)。
`tests/run.sh` 在新临时库上同时验证原有基线、002、数据保留与约束。

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
sqlite3 -batch -bail "$demo_database" < database/seeds/expansion_20_20_100_200.sql
sqlite3 -batch -bail "$demo_database" < database/tests/verify_demo.sql
sqlite3 -batch -bail "$demo_database" < database/tests/verify_expansion.sql
```

最后两条命令分别输出 `database verification: OK` 和 `expansion verification: OK`，表示默认扩容数据库通过验证。迁移只对新库执行一次；两个种子均可重复执行且不会重复插入固定演示数据。

需要扩大演示数据时，在执行 `demo.sql` 后再执行：

```bash
sqlite3 "$demo_database" < database/seeds/expansion_20_20_100_200.sql
sqlite3 "$demo_database" < database/tests/verify_expansion.sql
```

扩展种子中的站点名称、地址和坐标依据 2026-09-05 从 OpenStreetMap Nominatim 公开检索结果整理，数据许可遵循 OpenStreetMap attribution/ODbL；用户、电桩编号和订单是基于这些公开站点构造的演示数据，不代表真实用户或真实交易。扩展种子使用独立 ID 范围，可重复执行且不会改写基础演示记录。

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
