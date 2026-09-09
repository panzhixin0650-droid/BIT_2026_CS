#!/usr/bin/env bash

# 数据库脚本自测入口：任一步失败立即退出
set -euo pipefail

# 在临时目录建测试库，退出时自动清理
database_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
test_tmpdir="$(mktemp -d /tmp/bit-db-test.XXXXXX)"
test_database="$test_tmpdir/demo.db"

cleanup_database_test() {
    case "$test_tmpdir" in
        /tmp/bit-db-test.*) rm -r -- "$test_tmpdir" ;;
    esac
}
trap cleanup_database_test EXIT

if ! command -v sqlite3 >/dev/null 2>&1; then
    echo 'sqlite3 is required; see database/README.md' >&2
    exit 1
fi

# 辅助函数：断言某条SQL必须失败且错误信息匹配
expect_sql_failure() {
    local test_name="$1"
    local expected_message="$2"
    local sql="$3"
    local failure_output

    if failure_output="$(sqlite3 -batch -bail "$test_database" \
        "PRAGMA foreign_keys = ON; $sql" 2>&1)"; then
        printf 'FAIL: %s unexpectedly succeeded\n' "$test_name" >&2
        return 1
    fi

    if [[ "$failure_output" != *"$expected_message"* ]]; then
        printf 'FAIL: %s returned an unexpected error:\n%s\n' \
            "$test_name" "$failure_output" >&2
        return 1
    fi

    printf 'PASS: %s\n' "$test_name"
}

# 先建表导入种子，并验证种子可重复执行
sqlite3 -batch -bail "$test_database" \
    < "$database_dir/migrations/001_initial_demo.sql"
sqlite3 -batch -bail "$test_database" < "$database_dir/seeds/demo.sql"

# The development seed is intentionally safe to run twice.
sqlite3 -batch -bail "$test_database" < "$database_dir/seeds/demo.sql"
sqlite3 -batch -bail "$test_database" < "$database_dir/tests/verify_demo.sql"
sqlite3 -batch -bail "$test_database" < "$database_dir/tests/transaction_smoke.sql"

# 以下逐条验证手机号、外键、状态等约束会拒绝坏数据
expect_sql_failure \
    'invalid phone is rejected' \
    'ck_users_phone' \
    "INSERT INTO users VALUES (
        99, 'not-a-phone', '非法用户', 0, 'ACTIVE', '2026-09-03T00:00:00Z'
    );"

expect_sql_failure \
    'orphan pile is rejected' \
    'FOREIGN KEY constraint failed' \
    "INSERT INTO charging_piles VALUES (
        99, 999, 'PILE-X-99', 'FAST', 60.0, 'IDLE'
    );"

expect_sql_failure \
    'second current order for a user is rejected' \
    'UNIQUE constraint failed: charging_orders.user_id' \
    "INSERT INTO charging_orders VALUES (
        901, 'TEST-CURRENT-USER', 2, 7, 'DIRECT', 'CHARGING',
        NULL, '2026-09-03T00:00:00Z', NULL, NULL,
        0, 0, 120, 0, '2026-09-03T00:00:00Z'
    );"

expect_sql_failure \
    'second occupying order for a pile is rejected' \
    'UNIQUE constraint failed: charging_orders.pile_id' \
    "INSERT INTO charging_orders VALUES (
        902, 'TEST-CURRENT-PILE', 1, 3, 'RESERVATION', 'RESERVED',
        '2026-09-03T00:00:00Z', NULL, NULL, NULL,
        0, 0, NULL, 0, '2026-09-03T00:00:00Z'
    );"

expect_sql_failure \
    'inconsistent charging state is rejected' \
    'ck_orders_state_shape' \
    "INSERT INTO charging_orders VALUES (
        903, 'TEST-STATE-SHAPE', 1, 7, 'DIRECT', 'CHARGING',
        NULL, NULL, NULL, NULL,
        0, 0, 120, 0, '2026-09-03T00:00:00Z'
    );"

expect_sql_failure \
    'reservation after charging start is rejected' \
    'ck_orders_timestamp_order' \
    "INSERT INTO charging_orders VALUES (
        905, 'TEST-TIMESTAMP-ORDER', 1, 7, 'RESERVATION', 'CHARGING',
        '2026-09-03T00:20:00Z', '2026-09-03T00:10:00Z', NULL, NULL,
        0, 0, 120, 0, '2026-09-03T00:00:00Z'
    );"

expect_sql_failure \
    'incorrect amount is rejected' \
    'ck_orders_amount_formula' \
    "INSERT INTO charging_orders VALUES (
        904, 'TEST-AMOUNT', 1, 7, 'DIRECT', 'COMPLETED',
        NULL, '2026-09-03T00:00:00Z', '2026-09-03T01:00:00Z',
        '2026-09-03T01:00:00Z', 3600, 10000, 120, 1,
        '2026-09-03T00:00:00Z'
    );"

# Opt-in extension: baseline tests above still run against schema 1 unchanged.
# 记录业务表快照，确认后续迁移不改动原有数据
baseline_dump="$(sqlite3 "$test_database" '.dump users admins charging_stations charging_piles charging_orders')"
sqlite3 -batch -bail "$test_database" < "$database_dir/migrations/002_support_tickets.sql"
[[ "$(sqlite3 "$test_database" 'PRAGMA user_version')" == '2' ]]
[[ "$(sqlite3 "$test_database" '.dump users admins charging_stations charging_piles charging_orders')" == "$baseline_dump" ]]
sqlite3 -batch -bail "$test_database" < "$database_dir/tests/verify_support_tickets.sql"
if sqlite3 -batch -bail "$test_database" < "$database_dir/migrations/002_support_tickets.sql" 2>/dev/null; then
    echo 'FAIL: migration 002 must reject a non-v1 database' >&2
    exit 1
fi
[[ "$(sqlite3 "$test_database" 'PRAGMA integrity_check')" == 'ok' ]]
[[ -z "$(sqlite3 "$test_database" 'PRAGMA foreign_key_check')" ]]
ticket_business_dump="$(sqlite3 "$test_database" '.dump users charging_stations charging_piles charging_orders support_tickets')"
legacy_admins="$(sqlite3 "$test_database" 'SELECT admin_id, username, password_hash, display_name FROM admins ORDER BY admin_id;')"
sqlite3 -batch -bail "$test_database" < "$database_dir/migrations/003_admin_accounts.sql"
[[ "$(sqlite3 "$test_database" 'PRAGMA user_version')" == '3' ]]
[[ "$(sqlite3 "$test_database" '.dump users charging_stations charging_piles charging_orders support_tickets')" == "$ticket_business_dump" ]]
[[ "$(sqlite3 "$test_database" 'SELECT admin_id, username, password_hash, display_name FROM admins ORDER BY admin_id;')" == "$legacy_admins" ]]
sqlite3 -batch -bail "$test_database" < "$database_dir/tests/verify_admin_accounts.sql"
if sqlite3 -batch -bail "$test_database" < "$database_dir/migrations/003_admin_accounts.sql" 2>/dev/null; then
    echo 'FAIL: migration 003 must reject a non-v2 database' >&2
    exit 1
fi

# A missing prerequisite must fail without renaming any baseline tables.
# 另建旧库验证缺少前置迁移时不会改坏基线表
legacy_database="$test_tmpdir/legacy.db"
sqlite3 -batch -bail "$legacy_database" < "$database_dir/migrations/001_initial_demo.sql"
if sqlite3 -batch -bail "$legacy_database" < "$database_dir/migrations/003_admin_accounts.sql" 2>/dev/null; then
    echo 'FAIL: migration 003 must require ticket migration 002' >&2
    exit 1
fi
[[ "$(sqlite3 "$legacy_database" 'PRAGMA user_version')" == '1' ]]
sqlite3 -batch -bail "$legacy_database" < "$database_dir/migrations/002_support_tickets.sql"
# If a legacy account cannot satisfy the new constraints, the entire migration rolls back.
sqlite3 -batch -bail "$legacy_database" "INSERT INTO admins VALUES (99, 'invalid user', lower(hex(zeroblob(32))), 'legacy');"
if sqlite3 -batch -bail "$legacy_database" < "$database_dir/migrations/003_admin_accounts.sql" 2>/dev/null; then
    echo 'FAIL: invalid legacy account should require manual review' >&2
    exit 1
fi
[[ "$(sqlite3 "$legacy_database" 'PRAGMA user_version')" == '2' ]]
[[ "$(sqlite3 "$legacy_database" "SELECT count(*) FROM pragma_table_info('admins');")" == '4' ]]
[[ "$(sqlite3 "$legacy_database" "SELECT count(*) FROM admins WHERE username = 'invalid user';")" == '1' ]]
[[ "$(sqlite3 "$legacy_database" "SELECT count(*) FROM sqlite_schema WHERE name IN ('admins_v1', 'admin_station_scopes', 'admin_audit_logs');")" == '0' ]]
[[ "$(sqlite3 "$test_database" 'PRAGMA integrity_check')" == 'ok' ]]
[[ -z "$(sqlite3 "$test_database" 'PRAGMA foreign_key_check')" ]]
repair_business_dump="$(sqlite3 "$test_database" '.dump users admins charging_stations charging_piles charging_orders admin_station_scopes admin_audit_logs')"
old_ticket_rows="$(sqlite3 "$test_database" 'SELECT ticket_id, user_id, submission_id, title, summary, source_model, status, reply, created_at, updated_at FROM support_tickets ORDER BY ticket_id;')"
# 应用报修迁移并比对工单原有字段未变
sqlite3 -batch -bail "$test_database" < "$database_dir/migrations/004_repair_tickets.sql"
[[ "$(sqlite3 "$test_database" 'PRAGMA user_version')" == '4' ]]
[[ "$(sqlite3 "$test_database" '.dump users admins charging_stations charging_piles charging_orders admin_station_scopes admin_audit_logs')" == "$repair_business_dump" ]]
[[ "$(sqlite3 "$test_database" 'SELECT ticket_id, user_id, submission_id, title, summary, source_model, status, reply, created_at, updated_at FROM support_tickets ORDER BY ticket_id;')" == "$old_ticket_rows" ]]
sqlite3 -batch -bail "$test_database" < "$database_dir/tests/verify_repair_tickets.sql"
if sqlite3 -batch -bail "$test_database" < "$database_dir/migrations/004_repair_tickets.sql" 2>/dev/null; then
    echo 'FAIL: migration 004 must require schema 3' >&2
    exit 1
fi
expect_sql_failure 'repair requires an existing pile' 'FOREIGN KEY constraint failed' \
    "UPDATE support_tickets SET pile_code = 'MISSING' WHERE ticket_id = 990;"
expect_sql_failure 'repair requires a fault type' 'ck_ticket_repair' \
    "UPDATE support_tickets SET fault_type = '' WHERE ticket_id = 990;"
expect_sql_failure 'support cannot have a dangling fault type' 'ck_ticket_repair' \
    "UPDATE support_tickets SET pile_code = NULL WHERE ticket_id = 990;"
[[ "$(sqlite3 "$test_database" 'PRAGMA integrity_check')" == 'ok' ]]
[[ -z "$(sqlite3 "$test_database" 'PRAGMA foreign_key_check')" ]]
# 全部通过后输出各阶段汇总结果
echo 'database tests: OK (schema 1 baseline + schema 2 tickets + schema 3 administrators + schema 4 repairs)'
