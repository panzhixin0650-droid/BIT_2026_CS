-- Promote administrator account management and role-based access control.
-- Compatible with databases created by 001_initial_demo.sql.

PRAGMA foreign_keys = ON;

BEGIN IMMEDIATE;

ALTER TABLE admins RENAME TO admins_v1;

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

PRAGMA user_version = 2;

COMMIT;
