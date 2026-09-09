-- Promote administrator account management and role-based access control.
-- Requires the merged schema 2 (001 + 002_support_tickets.sql).
-- Stop the server, back up the database and run with sqlite3 -batch -bail.

-- 迁移3：升级管理员账号表，加入角色与站点授权
PRAGMA foreign_keys = ON;

BEGIN IMMEDIATE;

-- 守卫：要求版本为2且旧admins仍是四列结构
CREATE TEMP TABLE migration_003_guard (value INTEGER NOT NULL CHECK (value = 1));
INSERT INTO migration_003_guard
SELECT CASE WHEN user_version = 2
    AND (SELECT COUNT(*) FROM sqlite_schema WHERE type = 'table'
         AND name IN ('users', 'admins', 'charging_stations', 'charging_piles',
                      'charging_orders', 'support_tickets')) = 6
    AND (SELECT COUNT(*) FROM pragma_table_info('admins')) = 4
    AND NOT EXISTS (SELECT 1 FROM sqlite_schema WHERE name IN
                    ('admins_v1', 'admin_station_scopes', 'admin_audit_logs'))
    THEN 1 ELSE 0 END FROM pragma_user_version;
DROP TABLE migration_003_guard;

ALTER TABLE admins RENAME TO admins_v1;

-- 新管理员表：角色、状态、强制改密与版本号
CREATE TABLE admins (
    admin_id INTEGER PRIMARY KEY,
    username TEXT NOT NULL UNIQUE COLLATE NOCASE,
    password_hash TEXT NOT NULL,
    password_algorithm TEXT NOT NULL DEFAULT 'PBKDF2_SHA256',
    display_name TEXT NOT NULL,
    role TEXT NOT NULL DEFAULT 'SYS_ADMIN',
    status TEXT NOT NULL DEFAULT 'ACTIVE',
    must_change_password INTEGER NOT NULL DEFAULT 1,
    last_login_at TEXT,
    created_at TEXT NOT NULL,
    updated_at TEXT NOT NULL,
    version INTEGER NOT NULL DEFAULT 0,

    CONSTRAINT ck_admins_username_v2 CHECK (
        length(username) BETWEEN 3 AND 32
        AND username NOT GLOB '*[^A-Za-z0-9_.-]*'
    ),
    CONSTRAINT ck_admins_password_hash_v2 CHECK (length(password_hash) BETWEEN 32 AND 256),
    -- 口令允许遗留SHA256或新的PBKDF2两种格式
    CONSTRAINT ck_admins_password_algorithm CHECK (
        (password_algorithm = 'SHA256_LEGACY'
         AND length(password_hash) = 64
         AND password_hash = lower(password_hash)
         AND password_hash NOT GLOB '*[^0-9a-f]*')
        OR
        (password_algorithm = 'PBKDF2_SHA256'
         AND password_hash GLOB '$pbkdf2-sha256$*')
    ),
    CONSTRAINT ck_admins_display_name_v2 CHECK (
        length(trim(display_name)) BETWEEN 1 AND 32
    ),
    CONSTRAINT ck_admins_role CHECK (
        role IN ('SYS_ADMIN', 'STATION_ADMIN', 'USER_ADMIN')
    ),
    CONSTRAINT ck_admins_status CHECK (status IN ('ACTIVE', 'DISABLED')),
    CONSTRAINT ck_admins_must_change CHECK (must_change_password IN (0, 1)),
    CONSTRAINT ck_admins_version CHECK (version >= 0)
);

-- 把旧管理员数据搬入新表并标记为遗留算法
INSERT INTO admins (
    admin_id, username, password_hash, password_algorithm, display_name,
    role, status, must_change_password, last_login_at,
    created_at, updated_at, version
)
SELECT
    admin_id, username, password_hash, 'SHA256_LEGACY', display_name,
    'SYS_ADMIN', 'ACTIVE', 0, NULL,
    strftime('%Y-%m-%dT%H:%M:%SZ', 'now'),
    strftime('%Y-%m-%dT%H:%M:%SZ', 'now'), 0
FROM admins_v1;

DROP TABLE admins_v1;

-- 站点授权范围表：记录管理员可管理的站点
CREATE TABLE admin_station_scopes (
    admin_id INTEGER NOT NULL,
    station_id INTEGER NOT NULL,
    granted_by_admin_id INTEGER NOT NULL,
    granted_at TEXT NOT NULL,
    PRIMARY KEY (admin_id, station_id),
    CONSTRAINT fk_admin_scopes_admin FOREIGN KEY (admin_id)
        REFERENCES admins(admin_id) ON UPDATE RESTRICT ON DELETE CASCADE,
    CONSTRAINT fk_admin_scopes_station FOREIGN KEY (station_id)
        REFERENCES charging_stations(station_id) ON UPDATE RESTRICT ON DELETE RESTRICT,
    CONSTRAINT fk_admin_scopes_grantor FOREIGN KEY (granted_by_admin_id)
        REFERENCES admins(admin_id) ON UPDATE RESTRICT ON DELETE RESTRICT
);

-- 管理员操作审计表，详情要求为合法JSON
CREATE TABLE admin_audit_logs (
    audit_id INTEGER PRIMARY KEY,
    actor_admin_id INTEGER NOT NULL,
    action TEXT NOT NULL,
    target_admin_id INTEGER NOT NULL,
    details_json TEXT NOT NULL,
    created_at TEXT NOT NULL,
    CONSTRAINT fk_admin_audit_actor FOREIGN KEY (actor_admin_id)
        REFERENCES admins(admin_id) ON UPDATE RESTRICT ON DELETE RESTRICT,
    CONSTRAINT fk_admin_audit_target FOREIGN KEY (target_admin_id)
        REFERENCES admins(admin_id) ON UPDATE RESTRICT ON DELETE RESTRICT,
    CONSTRAINT ck_admin_audit_action CHECK (length(action) BETWEEN 1 AND 64),
    CONSTRAINT ck_admin_audit_details CHECK (json_valid(details_json))
);

CREATE INDEX idx_admins_role_status ON admins(role, status);
CREATE INDEX idx_admin_scopes_station ON admin_station_scopes(station_id, admin_id);
CREATE INDEX idx_admin_audit_target_time
    ON admin_audit_logs(target_admin_id, created_at DESC);

-- schema版本升级为3
PRAGMA user_version = 3;

COMMIT;
