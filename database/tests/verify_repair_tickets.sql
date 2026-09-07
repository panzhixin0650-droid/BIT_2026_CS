PRAGMA foreign_keys = ON;
INSERT INTO support_tickets
    (ticket_id, user_id, submission_id, title, summary, source_model,
     created_at, updated_at, pile_code, fault_type)
VALUES (990, 1, 'd09f491a-e13f-409f-9ff4-7d56a414ef41', '设备报修', '屏幕无响应', '',
        '2026-09-07T00:00:00Z', '2026-09-07T00:00:00Z', 'PILE-A-01', '屏幕异常');
CREATE TEMP TABLE assert_repair (value INTEGER CHECK (value = 1));
INSERT INTO assert_repair SELECT count(*) = 1 FROM support_tickets
    WHERE ticket_id = 990 AND status = 'OPEN' AND reply = '' AND pile_code = 'PILE-A-01';
INSERT INTO assert_repair SELECT count(*) = 0 FROM support_tickets
    WHERE ticket_id <> 990 AND (pile_code IS NOT NULL OR fault_type <> '');
DROP TABLE assert_repair;
