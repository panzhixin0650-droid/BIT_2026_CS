-- Stop the server and back up the database. Apply with sqlite3 -batch -bail.
PRAGMA foreign_keys = ON;
BEGIN IMMEDIATE;
CREATE TEMP TABLE migration_004_guard (value INTEGER NOT NULL CHECK (value = 3));
INSERT INTO migration_004_guard SELECT user_version FROM pragma_user_version;
DROP TABLE migration_004_guard;

ALTER TABLE support_tickets ADD COLUMN pile_code TEXT
    REFERENCES charging_piles(pile_code) ON DELETE RESTRICT ON UPDATE RESTRICT;
ALTER TABLE support_tickets ADD COLUMN fault_type TEXT NOT NULL DEFAULT ''
    CONSTRAINT ck_ticket_repair CHECK (
        (pile_code IS NULL AND fault_type = '') OR
        (pile_code IS NOT NULL AND length(pile_code) BETWEEN 1 AND 64
         AND length(trim(fault_type)) BETWEEN 1 AND 64));
CREATE INDEX idx_support_repair_pile ON support_tickets(pile_code)
    WHERE pile_code IS NOT NULL;
PRAGMA user_version = 4;
COMMIT;
