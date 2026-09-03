-- BIT_2026_CS Demo database schema, version 1.
-- Apply once to a new SQLite database. This migration intentionally creates
-- only the five business tables defined by the current Demo baseline.

PRAGMA foreign_keys = ON;

BEGIN IMMEDIATE;

CREATE TABLE users (
    user_id INTEGER PRIMARY KEY,
    phone TEXT NOT NULL UNIQUE,
    nickname TEXT NOT NULL,
    balance_cents INTEGER NOT NULL DEFAULT 0,
    status TEXT NOT NULL DEFAULT 'ACTIVE',
    created_at TEXT NOT NULL,

    CONSTRAINT ck_users_phone CHECK (
        length(phone) = 11
        AND phone NOT GLOB '*[^0-9]*'
    ),
    CONSTRAINT ck_users_nickname CHECK (length(nickname) BETWEEN 1 AND 32),
    CONSTRAINT ck_users_balance CHECK (
        typeof(balance_cents) = 'integer'
        AND balance_cents >= 0
    ),
    CONSTRAINT ck_users_status CHECK (status IN ('ACTIVE', 'FROZEN')),
    CONSTRAINT ck_users_created_at CHECK (
        length(created_at) = 20
        AND created_at GLOB
            '[0-9][0-9][0-9][0-9]-[0-9][0-9]-[0-9][0-9]T[0-9][0-9]:[0-9][0-9]:[0-9][0-9]Z'
    )
);

CREATE TABLE admins (
    admin_id INTEGER PRIMARY KEY,
    username TEXT NOT NULL UNIQUE,
    password_hash TEXT NOT NULL,
    display_name TEXT NOT NULL,

    CONSTRAINT ck_admins_username CHECK (length(username) BETWEEN 1 AND 64),
    CONSTRAINT ck_admins_password_hash CHECK (
        length(password_hash) = 64
        AND password_hash = lower(password_hash)
        AND password_hash NOT GLOB '*[^0-9a-f]*'
    ),
    CONSTRAINT ck_admins_display_name CHECK (length(display_name) BETWEEN 1 AND 64)
);

CREATE TABLE charging_stations (
    station_id INTEGER PRIMARY KEY,
    name TEXT NOT NULL,
    region TEXT NOT NULL,
    address TEXT NOT NULL,
    longitude REAL NOT NULL,
    latitude REAL NOT NULL,
    price_cents_per_kwh INTEGER NOT NULL,
    status TEXT NOT NULL DEFAULT 'ACTIVE',
    created_at TEXT NOT NULL,

    CONSTRAINT ck_stations_name CHECK (length(name) BETWEEN 1 AND 64),
    CONSTRAINT ck_stations_region CHECK (length(region) BETWEEN 1 AND 64),
    CONSTRAINT ck_stations_address CHECK (length(address) BETWEEN 1 AND 200),
    CONSTRAINT ck_stations_longitude CHECK (
        typeof(longitude) IN ('integer', 'real')
        AND longitude BETWEEN -180.0 AND 180.0
    ),
    CONSTRAINT ck_stations_latitude CHECK (
        typeof(latitude) IN ('integer', 'real')
        AND latitude BETWEEN -90.0 AND 90.0
    ),
    CONSTRAINT ck_stations_price CHECK (
        typeof(price_cents_per_kwh) = 'integer'
        AND price_cents_per_kwh > 0
    ),
    CONSTRAINT ck_stations_status CHECK (status IN ('ACTIVE', 'DISABLED')),
    CONSTRAINT ck_stations_created_at CHECK (
        length(created_at) = 20
        AND created_at GLOB
            '[0-9][0-9][0-9][0-9]-[0-9][0-9]-[0-9][0-9]T[0-9][0-9]:[0-9][0-9]:[0-9][0-9]Z'
    )
);

CREATE TABLE charging_piles (
    pile_id INTEGER PRIMARY KEY,
    station_id INTEGER NOT NULL,
    pile_code TEXT NOT NULL UNIQUE,
    pile_type TEXT NOT NULL,
    rated_power_kw REAL NOT NULL,
    status TEXT NOT NULL DEFAULT 'IDLE',

    CONSTRAINT fk_piles_station FOREIGN KEY (station_id)
        REFERENCES charging_stations(station_id)
        ON UPDATE RESTRICT
        ON DELETE RESTRICT,
    CONSTRAINT ck_piles_code CHECK (length(pile_code) BETWEEN 1 AND 64),
    CONSTRAINT ck_piles_type CHECK (pile_type IN ('FAST', 'SLOW')),
    CONSTRAINT ck_piles_power CHECK (
        typeof(rated_power_kw) IN ('integer', 'real')
        AND rated_power_kw > 0
    ),
    CONSTRAINT ck_piles_status CHECK (
        status IN ('IDLE', 'RESERVED', 'CHARGING', 'FAULT', 'OFFLINE')
    )
);

CREATE TABLE charging_orders (
    order_id INTEGER PRIMARY KEY,
    order_no TEXT NOT NULL UNIQUE,
    user_id INTEGER NOT NULL,
    pile_id INTEGER NOT NULL,
    mode TEXT NOT NULL,
    status TEXT NOT NULL,
    reserved_at TEXT,
    started_at TEXT,
    ended_at TEXT,
    paid_at TEXT,
    duration_seconds INTEGER NOT NULL DEFAULT 0,
    energy_wh INTEGER NOT NULL DEFAULT 0,
    unit_price_cents_per_kwh INTEGER,
    amount_cents INTEGER NOT NULL DEFAULT 0,
    created_at TEXT NOT NULL,

    CONSTRAINT fk_orders_user FOREIGN KEY (user_id)
        REFERENCES users(user_id)
        ON UPDATE RESTRICT
        ON DELETE RESTRICT,
    CONSTRAINT fk_orders_pile FOREIGN KEY (pile_id)
        REFERENCES charging_piles(pile_id)
        ON UPDATE RESTRICT
        ON DELETE RESTRICT,
    CONSTRAINT ck_orders_number CHECK (length(order_no) BETWEEN 1 AND 64),
    CONSTRAINT ck_orders_mode CHECK (mode IN ('RESERVATION', 'DIRECT')),
    CONSTRAINT ck_orders_status CHECK (
        status IN (
            'RESERVED',
            'CHARGING',
            'PENDING_PAYMENT',
            'COMPLETED',
            'CANCELLED'
        )
    ),
    CONSTRAINT ck_orders_reserved_at CHECK (
        reserved_at IS NULL
        OR (
            length(reserved_at) = 20
            AND reserved_at GLOB
                '[0-9][0-9][0-9][0-9]-[0-9][0-9]-[0-9][0-9]T[0-9][0-9]:[0-9][0-9]:[0-9][0-9]Z'
        )
    ),
    CONSTRAINT ck_orders_started_at CHECK (
        started_at IS NULL
        OR (
            length(started_at) = 20
            AND started_at GLOB
                '[0-9][0-9][0-9][0-9]-[0-9][0-9]-[0-9][0-9]T[0-9][0-9]:[0-9][0-9]:[0-9][0-9]Z'
        )
    ),
    CONSTRAINT ck_orders_ended_at CHECK (
        ended_at IS NULL
        OR (
            length(ended_at) = 20
            AND ended_at GLOB
                '[0-9][0-9][0-9][0-9]-[0-9][0-9]-[0-9][0-9]T[0-9][0-9]:[0-9][0-9]:[0-9][0-9]Z'
        )
    ),
    CONSTRAINT ck_orders_paid_at CHECK (
        paid_at IS NULL
        OR (
            length(paid_at) = 20
            AND paid_at GLOB
                '[0-9][0-9][0-9][0-9]-[0-9][0-9]-[0-9][0-9]T[0-9][0-9]:[0-9][0-9]:[0-9][0-9]Z'
        )
    ),
    CONSTRAINT ck_orders_created_at CHECK (
        length(created_at) = 20
        AND created_at GLOB
            '[0-9][0-9][0-9][0-9]-[0-9][0-9]-[0-9][0-9]T[0-9][0-9]:[0-9][0-9]:[0-9][0-9]Z'
    ),
    CONSTRAINT ck_orders_duration CHECK (
        typeof(duration_seconds) = 'integer'
        AND duration_seconds >= 0
    ),
    CONSTRAINT ck_orders_energy CHECK (
        typeof(energy_wh) = 'integer'
        AND energy_wh >= 0
    ),
    CONSTRAINT ck_orders_unit_price CHECK (
        unit_price_cents_per_kwh IS NULL
        OR (
            typeof(unit_price_cents_per_kwh) = 'integer'
            AND unit_price_cents_per_kwh > 0
        )
    ),
    CONSTRAINT ck_orders_amount CHECK (
        typeof(amount_cents) = 'integer'
        AND amount_cents >= 0
    ),
    CONSTRAINT ck_orders_mode_timestamps CHECK (
        (mode = 'RESERVATION' AND reserved_at IS NOT NULL)
        OR (mode = 'DIRECT' AND reserved_at IS NULL)
    ),
    CONSTRAINT ck_orders_timestamp_order CHECK (
        (reserved_at IS NULL OR created_at <= reserved_at)
        AND (started_at IS NULL OR created_at <= started_at)
        AND (reserved_at IS NULL OR started_at IS NULL OR reserved_at <= started_at)
        AND (ended_at IS NULL OR (started_at IS NOT NULL AND started_at <= ended_at))
        AND (paid_at IS NULL OR (ended_at IS NOT NULL AND ended_at <= paid_at))
    ),
    CONSTRAINT ck_orders_state_shape CHECK (
        (
            status = 'RESERVED'
            AND mode = 'RESERVATION'
            AND started_at IS NULL
            AND ended_at IS NULL
            AND paid_at IS NULL
            AND unit_price_cents_per_kwh IS NULL
            AND duration_seconds = 0
            AND energy_wh = 0
            AND amount_cents = 0
        )
        OR (
            status = 'CHARGING'
            AND started_at IS NOT NULL
            AND ended_at IS NULL
            AND paid_at IS NULL
            AND unit_price_cents_per_kwh IS NOT NULL
        )
        OR (
            status = 'PENDING_PAYMENT'
            AND started_at IS NOT NULL
            AND ended_at IS NOT NULL
            AND paid_at IS NULL
            AND unit_price_cents_per_kwh IS NOT NULL
        )
        OR (
            status = 'COMPLETED'
            AND started_at IS NOT NULL
            AND ended_at IS NOT NULL
            AND paid_at IS NOT NULL
            AND unit_price_cents_per_kwh IS NOT NULL
        )
        OR (
            status = 'CANCELLED'
            AND mode = 'RESERVATION'
            AND started_at IS NULL
            AND ended_at IS NULL
            AND paid_at IS NULL
            AND unit_price_cents_per_kwh IS NULL
            AND duration_seconds = 0
            AND energy_wh = 0
            AND amount_cents = 0
        )
    ),
    CONSTRAINT ck_orders_amount_formula CHECK (
        unit_price_cents_per_kwh IS NULL
        OR amount_cents = ((energy_wh * unit_price_cents_per_kwh + 500) / 1000)
    )
);

-- Repository read paths and foreign-key lookups.
CREATE INDEX idx_stations_status_region
    ON charging_stations(status, region, station_id);

CREATE INDEX idx_piles_station_status
    ON charging_piles(station_id, status, pile_id);

CREATE INDEX idx_orders_user_created
    ON charging_orders(user_id, created_at DESC, order_id DESC);

CREATE INDEX idx_orders_pile_status
    ON charging_orders(pile_id, status);

CREATE INDEX idx_orders_status_paid_at
    ON charging_orders(status, paid_at);

-- Last-line defenses for the two current-order invariants. ApplicationService
-- still owns validation and transaction orchestration.
CREATE UNIQUE INDEX ux_orders_one_current_per_user
    ON charging_orders(user_id)
    WHERE status IN ('RESERVED', 'CHARGING', 'PENDING_PAYMENT');

CREATE UNIQUE INDEX ux_orders_one_occupied_per_pile
    ON charging_orders(pile_id)
    WHERE status IN ('RESERVED', 'CHARGING');

PRAGMA user_version = 1;

COMMIT;
