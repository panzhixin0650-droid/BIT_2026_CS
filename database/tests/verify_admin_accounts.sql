-- 校验管理员迁移结果的断言脚本
.bail on
PRAGMA foreign_keys = ON;
BEGIN;
CREATE TEMP TABLE admin_assertion (
    name TEXT NOT NULL,
    passed INTEGER NOT NULL CHECK (passed = 1)
);
-- 确认版本为3、共八张业务表且旧账号信息保留
INSERT INTO admin_assertion VALUES ('schema 3', (SELECT user_version = 3 FROM pragma_user_version));
INSERT INTO admin_assertion VALUES ('eight business tables', (
    SELECT COUNT(*) = 8 FROM sqlite_schema WHERE type = 'table' AND name NOT LIKE 'sqlite_%'
));
INSERT INTO admin_assertion VALUES ('legacy administrator preserved', (
    SELECT COUNT(*) = 1 FROM admins WHERE admin_id = 1 AND username = 'admin'
    AND role = 'SYS_ADMIN' AND status = 'ACTIVE' AND must_change_password = 0
    AND password_algorithm = 'SHA256_LEGACY'
));
INSERT INTO admin_assertion VALUES ('integrity', (SELECT integrity_check = 'ok' FROM pragma_integrity_check));
INSERT INTO admin_assertion VALUES ('foreign keys', NOT EXISTS (SELECT 1 FROM pragma_foreign_key_check));
-- 迁移本身不应生成授权范围或审计记录
INSERT INTO admin_assertion VALUES ('no scopes created implicitly', (SELECT COUNT(*) = 0 FROM admin_station_scopes));
INSERT INTO admin_assertion VALUES ('no audits fabricated by migration', (SELECT COUNT(*) = 0 FROM admin_audit_logs));
DROP TABLE admin_assertion;
COMMIT;
SELECT 'administrator migration verification: OK';
