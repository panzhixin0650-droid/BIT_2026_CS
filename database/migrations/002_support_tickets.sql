-- Incremental, opt-in migration. Back up the database and stop the server first.
-- Run with sqlite3 -batch -bail. Never apply to an uninitialized or newer schema.
-- 增量迁移脚本：在五表基线上新增客服工单表
PRAGMA foreign_keys = ON;
BEGIN IMMEDIATE;
-- 守卫表：仅当版本为1且五张基线表齐全时才继续
CREATE TEMP TABLE migration_002_guard (value INTEGER NOT NULL CHECK (value = 1));
INSERT INTO migration_002_guard
SELECT CASE WHEN user_version = 1 AND (
    SELECT COUNT(*) FROM sqlite_schema WHERE type = 'table'
    AND name IN ('users', 'admins', 'charging_stations', 'charging_piles', 'charging_orders')
) = 5 THEN 1 ELSE 0 END FROM pragma_user_version;
DROP TABLE migration_002_guard;

-- 工单表：保存用户确认的标题摘要与处理状态
CREATE TABLE support_tickets (
    ticket_id INTEGER PRIMARY KEY,
    user_id INTEGER NOT NULL REFERENCES users(user_id),
    submission_id TEXT NOT NULL CHECK (length(submission_id) = 36),
    title TEXT NOT NULL CHECK (length(trim(title)) BETWEEN 1 AND 80),
    summary TEXT NOT NULL CHECK (length(trim(summary)) BETWEEN 1 AND 4000),
    source_model TEXT NOT NULL DEFAULT '' CHECK (length(source_model) <= 120),
    -- 状态限定三种，新建默认为OPEN
    status TEXT NOT NULL DEFAULT 'OPEN' CHECK (status IN ('OPEN', 'IN_PROGRESS', 'RESOLVED')),
    reply TEXT NOT NULL DEFAULT '' CHECK (length(reply) <= 2000),
    created_at TEXT NOT NULL,
    updated_at TEXT NOT NULL,
    CONSTRAINT uq_support_submission UNIQUE (user_id, submission_id),
    -- 标记已解决时必须填写回复内容
    CONSTRAINT ck_support_resolved_reply CHECK (status <> 'RESOLVED' OR length(trim(reply)) > 0)
);
CREATE INDEX idx_support_user_ticket ON support_tickets(user_id, ticket_id DESC);
-- schema版本升级为2
PRAGMA user_version = 2;
COMMIT;
