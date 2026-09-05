.bail on
PRAGMA foreign_keys = ON;
BEGIN;
CREATE TEMP TABLE expansion_assertion(name TEXT NOT NULL, passed INTEGER NOT NULL CHECK(passed=1));
INSERT INTO expansion_assertion VALUES ('foreign keys enabled', (SELECT foreign_keys=1 FROM pragma_foreign_keys));
INSERT INTO expansion_assertion VALUES ('20 additional users', (SELECT count(*)=20 FROM users WHERE user_id BETWEEN 6 AND 25));
INSERT INTO expansion_assertion VALUES ('20 additional stations', (SELECT count(*)=20 FROM charging_stations WHERE station_id BETWEEN 4 AND 23));
INSERT INTO expansion_assertion VALUES ('100 additional piles', (SELECT count(*)=100 FROM charging_piles WHERE pile_id BETWEEN 13 AND 112));
INSERT INTO expansion_assertion VALUES ('200 additional orders', (SELECT count(*)=200 FROM charging_orders WHERE order_id BETWEEN 1101 AND 1300));
INSERT INTO expansion_assertion VALUES ('all expansion piles reference expansion stations', (SELECT count(*)=100 FROM charging_piles WHERE pile_id BETWEEN 13 AND 112 AND station_id BETWEEN 4 AND 23));
INSERT INTO expansion_assertion VALUES ('all expansion orders reference valid users and piles', NOT EXISTS (
    SELECT 1 FROM charging_orders o
    LEFT JOIN users u ON u.user_id=o.user_id
    LEFT JOIN charging_piles p ON p.pile_id=o.pile_id
    WHERE o.order_id BETWEEN 1101 AND 1300 AND (u.user_id IS NULL OR p.pile_id IS NULL)
));
INSERT INTO expansion_assertion VALUES ('amount formula', NOT EXISTS (
    SELECT 1 FROM charging_orders
    WHERE order_id BETWEEN 1101 AND 1300
      AND amount_cents <> ((energy_wh * unit_price_cents_per_kwh + 500) / 1000)
));
INSERT INTO expansion_assertion VALUES ('city locations present', (SELECT count(*)=20 FROM charging_stations WHERE station_id BETWEEN 4 AND 23 AND latitude BETWEEN -90 AND 90 AND longitude BETWEEN -180 AND 180));
ROLLBACK;
SELECT 'expansion verification: OK';
