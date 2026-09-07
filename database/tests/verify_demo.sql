.bail on

PRAGMA foreign_keys = ON;

BEGIN;

CREATE TEMP TABLE demo_assertion (
    assertion_name TEXT NOT NULL,
    passed INTEGER NOT NULL CHECK (passed = 1)
);

INSERT INTO demo_assertion VALUES (
    'foreign keys enabled for this connection',
    (SELECT foreign_keys = 1 FROM pragma_foreign_keys)
);

INSERT INTO demo_assertion VALUES (
    'database integrity',
    (
        SELECT count(*) = 1 AND min(integrity_check) = 'ok'
        FROM pragma_integrity_check
    )
);

INSERT INTO demo_assertion VALUES (
    'no foreign key violations',
    NOT EXISTS (SELECT 1 FROM pragma_foreign_key_check)
);

INSERT INTO demo_assertion VALUES (
    'schema version is 1',
    (SELECT user_version = 1 FROM pragma_user_version)
);

INSERT INTO demo_assertion VALUES (
    'exactly five business tables',
    (
        SELECT count(*) = 5
        FROM sqlite_schema
        WHERE type = 'table'
          AND name NOT LIKE 'sqlite_%'
    )
);

INSERT INTO demo_assertion VALUES ('five demo users', (SELECT count(*) = 5 FROM users));
INSERT INTO demo_assertion VALUES ('one demo admin', (SELECT count(*) = 1 FROM admins));
INSERT INTO demo_assertion VALUES (
    'three demo stations',
    (SELECT count(*) = 3 FROM charging_stations)
);
INSERT INTO demo_assertion VALUES (
    'twelve demo piles',
    (SELECT count(*) = 12 FROM charging_piles)
);
INSERT INTO demo_assertion VALUES (
    'nine completed orders',
    (SELECT count(*) = 9 FROM charging_orders WHERE status = 'COMPLETED')
);

INSERT INTO demo_assertion VALUES (
    'shared login fixture identity',
    EXISTS (
        SELECT 1
        FROM users
        WHERE user_id = 1
          AND phone = '13800000001'
          AND nickname = '演示用户0001'
          AND balance_cents = 20000
          AND status = 'ACTIVE'
    )
);

INSERT INTO demo_assertion VALUES (
    'shared station and pile identifiers',
    EXISTS (
        SELECT 1
        FROM charging_piles AS p
        JOIN charging_stations AS s ON s.station_id = p.station_id
        WHERE p.pile_code = 'PILE-A-01'
          AND p.status = 'IDLE'
          AND s.station_id = 1
          AND s.name = '浑南演示充电站'
          AND s.price_cents_per_kwh = 135
    )
);

INSERT INTO demo_assertion VALUES (
    'station one aggregate matches the shared fixture',
    EXISTS (
        SELECT 1
        FROM charging_stations AS s
        LEFT JOIN charging_piles AS p ON p.station_id = s.station_id
        WHERE s.station_id = 1
        GROUP BY s.station_id
        HAVING count(p.pile_id) = 2
           AND sum(CASE WHEN p.status = 'IDLE' THEN 1 ELSE 0 END) = 1
           AND sum(CASE WHEN p.status <> 'OFFLINE' THEN 1 ELSE 0 END) = 2
    )
);

INSERT INTO demo_assertion VALUES (
    'pile history aggregates match the shared fixture',
    (
        SELECT count(*) = 2
        FROM (
            SELECT
                p.pile_code,
                count(o.order_id) AS charge_count,
                coalesce(sum(o.duration_seconds), 0) AS total_charge_seconds
            FROM charging_piles AS p
            LEFT JOIN charging_orders AS o
              ON o.pile_id = p.pile_id
             AND o.status IN ('PENDING_PAYMENT', 'COMPLETED')
            WHERE p.pile_code IN ('PILE-A-01', 'PILE-A-02')
            GROUP BY p.pile_id
            HAVING (p.pile_code = 'PILE-A-01' AND charge_count = 4 AND total_charge_seconds = 14400)
                OR (p.pile_code = 'PILE-A-02' AND charge_count = 2 AND total_charge_seconds = 7200)
        )
    )
);

INSERT INTO demo_assertion VALUES (
    'dashboard pile-state categories are populated',
    EXISTS (
        SELECT 1
        FROM (
            SELECT
                sum(CASE WHEN status = 'IDLE' THEN 1 ELSE 0 END) AS idle,
                sum(CASE WHEN status IN ('RESERVED', 'CHARGING') THEN 1 ELSE 0 END) AS in_use,
                sum(CASE WHEN status IN ('FAULT', 'OFFLINE') THEN 1 ELSE 0 END) AS fault
            FROM charging_piles
        )
        WHERE idle = 7
          AND in_use = 2
          AND fault = 3
    )
);

INSERT INTO demo_assertion VALUES (
    'occupied orders agree with pile state',
    NOT EXISTS (
        SELECT 1
        FROM charging_orders AS o
        JOIN charging_piles AS p ON p.pile_id = o.pile_id
        WHERE o.status IN ('RESERVED', 'CHARGING')
          AND o.status <> p.status
    )
);

INSERT INTO demo_assertion VALUES (
    'pending-payment piles are released',
    NOT EXISTS (
        SELECT 1
        FROM charging_orders AS o
        JOIN charging_piles AS p ON p.pile_id = o.pile_id
        WHERE o.status = 'PENDING_PAYMENT'
          AND p.status <> 'IDLE'
    )
);

INSERT INTO demo_assertion VALUES (
    'one current order at most per user',
    NOT EXISTS (
        SELECT user_id
        FROM charging_orders
        WHERE status IN ('RESERVED', 'CHARGING', 'PENDING_PAYMENT')
        GROUP BY user_id
        HAVING count(*) > 1
    )
);

INSERT INTO demo_assertion VALUES (
    'one occupying order at most per pile',
    NOT EXISTS (
        SELECT pile_id
        FROM charging_orders
        WHERE status IN ('RESERVED', 'CHARGING')
        GROUP BY pile_id
        HAVING count(*) > 1
    )
);

INSERT INTO demo_assertion VALUES (
    'all stored amounts use the V1 formula',
    NOT EXISTS (
        SELECT 1
        FROM charging_orders
        WHERE unit_price_cents_per_kwh IS NOT NULL
          AND amount_cents <> ((energy_wh * unit_price_cents_per_kwh + 500) / 1000)
    )
);

INSERT INTO demo_assertion VALUES (
    'recent revenue covers several business days',
    (
        SELECT count(DISTINCT date(paid_at, '+8 hours')) >= 5
        FROM charging_orders
        WHERE status = 'COMPLETED'
          AND date(paid_at, '+8 hours') BETWEEN
              date('now', '+8 hours', '-29 days') AND date('now', '+8 hours')
    )
);

INSERT INTO demo_assertion VALUES (
    'required partial unique indexes exist',
    (
        SELECT count(*) = 2
        FROM sqlite_schema
        WHERE type = 'index'
          AND name IN (
              'ux_orders_one_current_per_user',
              'ux_orders_one_occupied_per_pile'
          )
    )
);

ROLLBACK;

SELECT 'database verification: OK';
