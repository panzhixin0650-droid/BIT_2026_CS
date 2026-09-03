# 数据库验证

在仓库根目录运行：

```bash
database/tests/run.sh
```

脚本只依赖 Bash 和 SQLite 3。它在 `/tmp` 下创建全新的临时数据库，依次执行首个迁移、两次演示种子和只读验收，再执行“预约 → 开始 → 停止并扣款”事务冒烟，最后验证手机号、外键、当前订单唯一性、订单状态与时间顺序及金额公式的失败分支。临时数据库在脚本退出时删除，不会写入仓库。

`verify_demo.sql` 也可以单独对已经初始化的开发库执行：

```bash
sqlite3 -batch -bail build/database/demo.db < database/tests/verify_demo.sql
```
