#!/usr/bin/env bash

set -euo pipefail

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

sqlite3 -batch -bail "$test_database" \
    < "$database_dir/migrations/001_initial_demo.sql"
sqlite3 -batch -bail "$test_database" < "$database_dir/seeds/demo.sql"

# The development seed is intentionally safe to run twice.
sqlite3 -batch -bail "$test_database" < "$database_dir/seeds/demo.sql"
sqlite3 -batch -bail "$test_database" < "$database_dir/tests/verify_demo.sql"
sqlite3 -batch -bail "$test_database" < "$database_dir/tests/transaction_smoke.sql"

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

echo 'database tests: OK'
