.bail on

PRAGMA foreign_keys = ON;

CREATE TEMP TABLE transaction_assertion (
    assertion_name TEXT NOT NULL,
    passed INTEGER NOT NULL CHECK (passed = 1)
);

-- Reserve PILE-A-01 for the shared Demo user.
BEGIN IMMEDIATE;

INSERT INTO charging_orders (
    order_id,
    order_no,
    user_id,
    pile_id,
    mode,
    status,
    reserved_at,
    started_at,
    ended_at,
    paid_at,
    duration_seconds,
    energy_wh,
    unit_price_cents_per_kwh,
    amount_cents,
    created_at
) VALUES (
    301,
    'TEST-ORDER-FLOW-001',
    1,
    1,
    'RESERVATION',
    'RESERVED',
    strftime('%Y-%m-%dT%H:%M:%SZ', 'now', '-35 minutes'),
    NULL,
    NULL,
    NULL,
    0,
    0,
    NULL,
    0,
    strftime('%Y-%m-%dT%H:%M:%SZ', 'now', '-35 minutes')
);

UPDATE charging_piles
SET status = 'RESERVED'
WHERE pile_id = 1
  AND status = 'IDLE';

INSERT INTO transaction_assertion VALUES ('reserve changed one pile', changes() = 1);

COMMIT;

-- Start the reserved order and freeze the station price in the same transaction.
BEGIN IMMEDIATE;

UPDATE charging_orders
SET status = 'CHARGING',
    started_at = strftime('%Y-%m-%dT%H:%M:%SZ', 'now', '-30 minutes'),
    unit_price_cents_per_kwh = (
        SELECT s.price_cents_per_kwh
        FROM charging_piles AS p
        JOIN charging_stations AS s ON s.station_id = p.station_id
        WHERE p.pile_id = charging_orders.pile_id
    )
WHERE order_id = 301
  AND status = 'RESERVED';

INSERT INTO transaction_assertion VALUES ('start changed one order', changes() = 1);

UPDATE charging_piles
SET status = 'CHARGING'
WHERE pile_id = 1
  AND status = 'RESERVED';

INSERT INTO transaction_assertion VALUES ('start changed one pile', changes() = 1);

COMMIT;

-- Stop, release the pile, deduct the balance, and complete the order atomically.
BEGIN IMMEDIATE;

UPDATE charging_piles
SET status = 'IDLE'
WHERE pile_id = 1
  AND status = 'CHARGING';

INSERT INTO transaction_assertion VALUES ('stop released one pile', changes() = 1);

UPDATE users
SET balance_cents = balance_cents - 675
WHERE user_id = 1
  AND status = 'ACTIVE'
  AND balance_cents >= 675;

INSERT INTO transaction_assertion VALUES ('stop debited one user', changes() = 1);

UPDATE charging_orders
SET status = 'COMPLETED',
    ended_at = strftime('%Y-%m-%dT%H:%M:%SZ', 'now'),
    paid_at = strftime('%Y-%m-%dT%H:%M:%SZ', 'now'),
    duration_seconds = 1800,
    energy_wh = 5000,
    amount_cents = 675
WHERE order_id = 301
  AND status = 'CHARGING';

INSERT INTO transaction_assertion VALUES ('stop completed one order', changes() = 1);

COMMIT;

INSERT INTO transaction_assertion VALUES (
    'completed result matches the V1 fixture',
    EXISTS (
        SELECT 1
        FROM charging_orders AS o
        JOIN charging_piles AS p ON p.pile_id = o.pile_id
        JOIN users AS u ON u.user_id = o.user_id
        WHERE o.order_id = 301
          AND o.status = 'COMPLETED'
          AND o.energy_wh = 5000
          AND o.unit_price_cents_per_kwh = 135
          AND o.amount_cents = 675
          AND p.status = 'IDLE'
          AND u.balance_cents = 19325
    )
);

SELECT 'transaction smoke: OK';
