PRAGMA foreign_keys = ON;
CREATE TEMP TABLE assert_support (passed INTEGER CHECK (passed = 1));
INSERT INTO assert_support SELECT COUNT(*) = 0 FROM support_tickets;
BEGIN;
INSERT INTO support_tickets(user_id, submission_id, title, summary, created_at, updated_at)
VALUES (1, 'b758e849-0cd0-4eb6-8aee-35c5c98fd553', '测试工单', '用户确认的摘要',
        '2026-09-07T08:00:00Z', '2026-09-07T08:00:00Z');
INSERT OR IGNORE INTO support_tickets(user_id, submission_id, title, summary, created_at, updated_at)
VALUES (1, 'b758e849-0cd0-4eb6-8aee-35c5c98fd553', '重复提交', '摘要',
        '2026-09-07T08:00:00Z', '2026-09-07T08:00:00Z');
INSERT INTO assert_support SELECT COUNT(*) = 1 FROM support_tickets;
INSERT OR IGNORE INTO support_tickets(user_id, submission_id, title, summary, status, created_at, updated_at)
VALUES (1, 'b758e849-0cd0-4eb6-8aee-35c5c98fd554', '无效状态', '摘要', 'UNKNOWN',
        '2026-09-07T08:00:00Z', '2026-09-07T08:00:00Z');
UPDATE OR IGNORE support_tickets SET status = 'RESOLVED', reply = '';
INSERT INTO assert_support SELECT status = 'OPEN' FROM support_tickets;
UPDATE support_tickets SET status = 'RESOLVED', reply = '已核实';
INSERT INTO assert_support SELECT status = 'RESOLVED' FROM support_tickets;
ROLLBACK;
INSERT INTO assert_support SELECT COUNT(*) = 0 FROM support_tickets;
DROP TABLE assert_support;
