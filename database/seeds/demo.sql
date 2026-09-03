-- Idempotent development data for the current five-table Demo.
-- Relative order dates are evaluated when this seed is first applied.

PRAGMA foreign_keys = ON;

BEGIN IMMEDIATE;

INSERT OR IGNORE INTO admins (
    admin_id,
    username,
    password_hash,
    display_name
) VALUES (
    1,
    'admin',
    '8d969eef6ecad3c29a3a629280e686cf0c3f5d5a86aff3ca12020c923adc6c92',
    '演示管理员'
);

INSERT OR IGNORE INTO users (
    user_id,
    phone,
    nickname,
    balance_cents,
    status,
    created_at
) VALUES
    (
        1,
        '13800000001',
        '演示用户0001',
        20000,
        'ACTIVE',
        strftime('%Y-%m-%dT%H:%M:%SZ', 'now', '-90 days')
    ),
    (
        2,
        '13800000002',
        '充电中用户',
        15000,
        'ACTIVE',
        strftime('%Y-%m-%dT%H:%M:%SZ', 'now', '-80 days')
    ),
    (
        3,
        '13800000003',
        '已预约用户',
        8000,
        'ACTIVE',
        strftime('%Y-%m-%dT%H:%M:%SZ', 'now', '-70 days')
    ),
    (
        4,
        '13800000004',
        '冻结用户',
        12000,
        'FROZEN',
        strftime('%Y-%m-%dT%H:%M:%SZ', 'now', '-60 days')
    ),
    (
        5,
        '13800000005',
        '待支付用户',
        100,
        'ACTIVE',
        strftime('%Y-%m-%dT%H:%M:%SZ', 'now', '-50 days')
    );

INSERT OR IGNORE INTO charging_stations (
    station_id,
    name,
    region,
    address,
    longitude,
    latitude,
    price_cents_per_kwh,
    status,
    created_at
) VALUES
    (
        1,
        '浑南演示充电站',
        '浑南区',
        '浑南区创新路1号',
        123.43,
        41.71,
        135,
        'ACTIVE',
        strftime('%Y-%m-%dT%H:%M:%SZ', 'now', '-120 days')
    ),
    (
        2,
        '和平演示充电站',
        '和平区',
        '和平区青年大街2号',
        123.40,
        41.79,
        120,
        'ACTIVE',
        strftime('%Y-%m-%dT%H:%M:%SZ', 'now', '-110 days')
    ),
    (
        3,
        '沈河演示充电站',
        '沈河区',
        '沈河区文化路3号',
        123.46,
        41.80,
        150,
        'DISABLED',
        strftime('%Y-%m-%dT%H:%M:%SZ', 'now', '-100 days')
    );

INSERT OR IGNORE INTO charging_piles (
    pile_id,
    station_id,
    pile_code,
    pile_type,
    rated_power_kw,
    status
) VALUES
    (1, 1, 'PILE-A-01', 'FAST', 10.0, 'IDLE'),
    (2, 1, 'PILE-A-02', 'SLOW', 7.0, 'CHARGING'),
    (3, 2, 'PILE-B-01', 'FAST', 60.0, 'RESERVED'),
    (4, 2, 'PILE-B-02', 'FAST', 60.0, 'IDLE'),
    (5, 2, 'PILE-B-03', 'SLOW', 7.0, 'FAULT'),
    (6, 2, 'PILE-B-04', 'FAST', 60.0, 'OFFLINE'),
    (7, 2, 'PILE-B-05', 'SLOW', 7.0, 'IDLE'),
    (8, 3, 'PILE-C-01', 'FAST', 60.0, 'IDLE'),
    (9, 3, 'PILE-C-02', 'SLOW', 7.0, 'IDLE'),
    (10, 3, 'PILE-C-03', 'FAST', 60.0, 'IDLE'),
    (11, 3, 'PILE-C-04', 'SLOW', 7.0, 'FAULT'),
    (12, 3, 'PILE-C-05', 'FAST', 60.0, 'IDLE');

-- Nine completed orders. Their paid_at values cover several of the latest 30
-- Asia/Shanghai business days. A-01 and A-02 totals also match the shared
-- station-detail fixture: 4 / 14400 seconds and 2 / 7200 seconds.
INSERT OR IGNORE INTO charging_orders (
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
) VALUES
    (
        101,
        'DEMO-COMPLETED-001',
        1,
        1,
        'RESERVATION',
        'COMPLETED',
        strftime('%Y-%m-%dT%H:%M:%SZ', 'now', '-35 minutes'),
        strftime('%Y-%m-%dT%H:%M:%SZ', 'now', '-30 minutes'),
        strftime('%Y-%m-%dT%H:%M:%SZ', 'now'),
        strftime('%Y-%m-%dT%H:%M:%SZ', 'now'),
        1800,
        5000,
        135,
        675,
        strftime('%Y-%m-%dT%H:%M:%SZ', 'now', '-35 minutes')
    ),
    (
        102,
        'DEMO-COMPLETED-002',
        2,
        2,
        'DIRECT',
        'COMPLETED',
        NULL,
        strftime('%Y-%m-%dT%H:%M:%SZ', date('now', '+8 hours', '-1 day'), '+1 hour'),
        strftime('%Y-%m-%dT%H:%M:%SZ', date('now', '+8 hours', '-1 day'), '+2 hours'),
        strftime('%Y-%m-%dT%H:%M:%SZ', date('now', '+8 hours', '-1 day'), '+2 hours'),
        3600,
        10000,
        135,
        1350,
        strftime('%Y-%m-%dT%H:%M:%SZ', date('now', '+8 hours', '-1 day'), '+55 minutes')
    ),
    (
        103,
        'DEMO-COMPLETED-003',
        4,
        4,
        'DIRECT',
        'COMPLETED',
        NULL,
        strftime('%Y-%m-%dT%H:%M:%SZ', date('now', '+8 hours', '-2 days'), '+1 hour', '+20 minutes'),
        strftime('%Y-%m-%dT%H:%M:%SZ', date('now', '+8 hours', '-2 days'), '+2 hours'),
        strftime('%Y-%m-%dT%H:%M:%SZ', date('now', '+8 hours', '-2 days'), '+2 hours'),
        2400,
        6000,
        120,
        720,
        strftime('%Y-%m-%dT%H:%M:%SZ', date('now', '+8 hours', '-2 days'), '+1 hour', '+15 minutes')
    ),
    (
        104,
        'DEMO-COMPLETED-004',
        1,
        1,
        'DIRECT',
        'COMPLETED',
        NULL,
        strftime('%Y-%m-%dT%H:%M:%SZ', date('now', '+8 hours', '-4 days'), '+1 hour'),
        strftime('%Y-%m-%dT%H:%M:%SZ', date('now', '+8 hours', '-4 days'), '+2 hours'),
        strftime('%Y-%m-%dT%H:%M:%SZ', date('now', '+8 hours', '-4 days'), '+2 hours'),
        3600,
        8000,
        135,
        1080,
        strftime('%Y-%m-%dT%H:%M:%SZ', date('now', '+8 hours', '-4 days'), '+55 minutes')
    ),
    (
        105,
        'DEMO-COMPLETED-005',
        3,
        2,
        'RESERVATION',
        'COMPLETED',
        strftime('%Y-%m-%dT%H:%M:%SZ', date('now', '+8 hours', '-6 days'), '+50 minutes'),
        strftime('%Y-%m-%dT%H:%M:%SZ', date('now', '+8 hours', '-6 days'), '+1 hour'),
        strftime('%Y-%m-%dT%H:%M:%SZ', date('now', '+8 hours', '-6 days'), '+2 hours'),
        strftime('%Y-%m-%dT%H:%M:%SZ', date('now', '+8 hours', '-6 days'), '+2 hours'),
        3600,
        8000,
        135,
        1080,
        strftime('%Y-%m-%dT%H:%M:%SZ', date('now', '+8 hours', '-6 days'), '+50 minutes')
    ),
    (
        106,
        'DEMO-COMPLETED-006',
        5,
        7,
        'DIRECT',
        'COMPLETED',
        NULL,
        strftime('%Y-%m-%dT%H:%M:%SZ', date('now', '+8 hours', '-10 days'), '+50 minutes'),
        strftime('%Y-%m-%dT%H:%M:%SZ', date('now', '+8 hours', '-10 days'), '+2 hours'),
        strftime('%Y-%m-%dT%H:%M:%SZ', date('now', '+8 hours', '-10 days'), '+2 hours'),
        4200,
        11000,
        120,
        1320,
        strftime('%Y-%m-%dT%H:%M:%SZ', date('now', '+8 hours', '-10 days'), '+45 minutes')
    ),
    (
        107,
        'DEMO-COMPLETED-007',
        1,
        1,
        'RESERVATION',
        'COMPLETED',
        strftime('%Y-%m-%dT%H:%M:%SZ', date('now', '+8 hours', '-15 days'), '+25 minutes'),
        strftime('%Y-%m-%dT%H:%M:%SZ', date('now', '+8 hours', '-15 days'), '+30 minutes'),
        strftime('%Y-%m-%dT%H:%M:%SZ', date('now', '+8 hours', '-15 days'), '+2 hours'),
        strftime('%Y-%m-%dT%H:%M:%SZ', date('now', '+8 hours', '-15 days'), '+2 hours'),
        5400,
        12000,
        135,
        1620,
        strftime('%Y-%m-%dT%H:%M:%SZ', date('now', '+8 hours', '-15 days'), '+25 minutes')
    ),
    (
        108,
        'DEMO-COMPLETED-008',
        4,
        8,
        'DIRECT',
        'COMPLETED',
        NULL,
        strftime('%Y-%m-%dT%H:%M:%SZ', date('now', '+8 hours', '-21 days'), '+1 hour'),
        strftime('%Y-%m-%dT%H:%M:%SZ', date('now', '+8 hours', '-21 days'), '+2 hours'),
        strftime('%Y-%m-%dT%H:%M:%SZ', date('now', '+8 hours', '-21 days'), '+2 hours'),
        3600,
        9600,
        150,
        1440,
        strftime('%Y-%m-%dT%H:%M:%SZ', date('now', '+8 hours', '-21 days'), '+55 minutes')
    ),
    (
        109,
        'DEMO-COMPLETED-009',
        1,
        1,
        'DIRECT',
        'COMPLETED',
        NULL,
        strftime('%Y-%m-%dT%H:%M:%SZ', date('now', '+8 hours', '-28 days'), '+1 hour'),
        strftime('%Y-%m-%dT%H:%M:%SZ', date('now', '+8 hours', '-28 days'), '+2 hours'),
        strftime('%Y-%m-%dT%H:%M:%SZ', date('now', '+8 hours', '-28 days'), '+2 hours'),
        3600,
        9000,
        135,
        1215,
        strftime('%Y-%m-%dT%H:%M:%SZ', date('now', '+8 hours', '-28 days'), '+55 minutes')
    );

-- Independent current-order scenarios for Repository and ApplicationService
-- development. Occupied pile states match their current order states.
INSERT OR IGNORE INTO charging_orders (
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
) VALUES
    (
        201,
        'DEMO-CHARGING-001',
        2,
        2,
        'DIRECT',
        'CHARGING',
        NULL,
        strftime('%Y-%m-%dT%H:%M:%SZ', 'now', '-30 minutes'),
        NULL,
        NULL,
        0,
        0,
        135,
        0,
        strftime('%Y-%m-%dT%H:%M:%SZ', 'now', '-35 minutes')
    ),
    (
        202,
        'DEMO-RESERVED-001',
        3,
        3,
        'RESERVATION',
        'RESERVED',
        strftime('%Y-%m-%dT%H:%M:%SZ', 'now', '-10 minutes'),
        NULL,
        NULL,
        NULL,
        0,
        0,
        NULL,
        0,
        strftime('%Y-%m-%dT%H:%M:%SZ', 'now', '-10 minutes')
    ),
    (
        203,
        'DEMO-PENDING-001',
        5,
        4,
        'DIRECT',
        'PENDING_PAYMENT',
        NULL,
        strftime('%Y-%m-%dT%H:%M:%SZ', 'now', '-2 hours'),
        strftime('%Y-%m-%dT%H:%M:%SZ', 'now', '-1 hour'),
        NULL,
        3600,
        20000,
        120,
        2400,
        strftime('%Y-%m-%dT%H:%M:%SZ', 'now', '-2 hours', '-5 minutes')
    );

COMMIT;
