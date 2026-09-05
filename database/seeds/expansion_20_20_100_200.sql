-- Additional deterministic development data.
-- Adds 20 stations, 20 users, 100 piles and 200 orders to demo.sql.
-- Station names, addresses and coordinates are based on public OpenStreetMap
-- Nominatim results queried on 2026-09-05; users, piles and orders are
-- synthetic records constructed from those real station locations.

PRAGMA foreign_keys = ON;
BEGIN IMMEDIATE;

INSERT OR IGNORE INTO users (user_id, phone, nickname, balance_cents, status, created_at)
WITH RECURSIVE n(x) AS (SELECT 1 UNION ALL SELECT x + 1 FROM n WHERE x < 20)
SELECT 5 + x, printf('1390000%04d', x), printf('城市演示用户%02d', x),
       CASE WHEN x IN (7, 14) THEN 500 ELSE 12000 + x * 350 END,
       CASE WHEN x IN (7, 14) THEN 'FROZEN' ELSE 'ACTIVE' END,
       strftime('%Y-%m-%dT%H:%M:%SZ', 'now', printf('-%d days', 20 + x))
FROM n;

INSERT OR IGNORE INTO charging_stations
    (station_id, name, region, address, longitude, latitude,
     price_cents_per_kwh, status, created_at)
VALUES
 (4,'国家电网西三环充电站','海淀区','北京市海淀区西三环甘家口街道',116.3041563,39.9215761,145,'ACTIVE',strftime('%Y-%m-%dT%H:%M:%SZ','now','-80 days')),
 (5,'中漕路充电站','徐汇区','上海市徐汇区中漕路创世纪花园',121.4287012,31.1866882,155,'ACTIVE',strftime('%Y-%m-%dT%H:%M:%SZ','now','-79 days')),
 (6,'人和恒充充电站','白云区','广州市白云区人和镇秀盛路',113.2985621,23.3202832,135,'ACTIVE',strftime('%Y-%m-%dT%H:%M:%SZ','now','-78 days')),
 (7,'特斯拉滨海超级充电站','南山区','深圳市南山区滨海大道辅路',113.9448869,22.5228751,168,'ACTIVE',strftime('%Y-%m-%dT%H:%M:%SZ','now','-77 days')),
 (8,'国家电网博园路充电站','余杭区','杭州市余杭区良渚街道博园路',120.1166483,30.3771910,142,'ACTIVE',strftime('%Y-%m-%dT%H:%M:%SZ','now','-76 days')),
 (9,'毕韵佶充电站','江宁区','南京市江宁区横溪街道宁丹公路',118.7578395,31.6985755,130,'ACTIVE',strftime('%Y-%m-%dT%H:%M:%SZ','now','-75 days')),
 (10,'雅迪犀浦充电站','郫都区','成都市郫都区犀浦街道精勤路',103.9830762,30.7643401,125,'ACTIVE',strftime('%Y-%m-%dT%H:%M:%SZ','now','-74 days')),
 (11,'渝快充电动汽车充电站','两江新区','重庆市两江新区宝圣湖街道食品城大道',106.6020294,29.6524413,138,'ACTIVE',strftime('%Y-%m-%dT%H:%M:%SZ','now','-73 days')),
 (12,'荟园北侧充电站','洪山区','武汉市洪山区狮子山街道红豆路',114.3573666,30.4718063,128,'ACTIVE',strftime('%Y-%m-%dT%H:%M:%SZ','now','-72 days')),
 (13,'莲湖数字产业园充电站','莲湖区','西安市莲湖区桃园路街道大庆路',108.8950163,34.2721220,132,'ACTIVE',strftime('%Y-%m-%dT%H:%M:%SZ','now','-71 days')),
 (14,'软件园汽车充电站','浑南区','沈阳市浑南区全运北路',123.4805935,41.6897148,135,'ACTIVE',strftime('%Y-%m-%dT%H:%M:%SZ','now','-70 days')),
 (15,'特来电庄河充电站','庄河市','大连市庄河市城关街道建设南一街',122.9732798,39.6592776,126,'ACTIVE',strftime('%Y-%m-%dT%H:%M:%SZ','now','-69 days')),
 (16,'理想哈尔滨路超级充电站','市北区','青岛市市北区双山街道哈尔滨路',120.3867371,36.1087466,150,'ACTIVE',strftime('%Y-%m-%dT%H:%M:%SZ','now','-68 days')),
 (17,'三五一九实业充电站','二七区','郑州市二七区建中街街道陇海中路',113.6457385,34.7379083,129,'ACTIVE',strftime('%Y-%m-%dT%H:%M:%SZ','now','-67 days')),
 (18,'星星充电汽车站','长沙县','长沙市长沙县星沙街道明月路',113.0737980,28.2684053,136,'ACTIVE',strftime('%Y-%m-%dT%H:%M:%SZ','now','-66 days')),
 (19,'文兴东路充电站','思明区','厦门市思明区莲前街道文兴东路',118.1764076,24.4666553,158,'ACTIVE',strftime('%Y-%m-%dT%H:%M:%SZ','now','-65 days')),
 (20,'亨通慧充众联充电站','吴江区','苏州市吴江区太湖新城镇联杨路',120.6348963,31.1288355,148,'ACTIVE',strftime('%Y-%m-%dT%H:%M:%SZ','now','-64 days')),
 (21,'中石油张家窝充电站','西青区','天津市西青区张家窝镇',117.0414641,39.0721072,140,'ACTIVE',strftime('%Y-%m-%dT%H:%M:%SZ','now','-63 days')),
 (22,'安坤智能充电站','历下区','济南市历下区姚家街道泺邑路',117.0882105,36.6669858,133,'ACTIVE',strftime('%Y-%m-%dT%H:%M:%SZ','now','-62 days')),
 (23,'合肥城市公共充电站','蜀山区','合肥市蜀山区创新大道',117.2830420,31.8611900,131,'ACTIVE',strftime('%Y-%m-%dT%H:%M:%SZ','now','-61 days'));

INSERT OR IGNORE INTO charging_piles
    (pile_id, station_id, pile_code, pile_type, rated_power_kw, status)
WITH RECURSIVE n(x) AS (SELECT 1 UNION ALL SELECT x + 1 FROM n WHERE x < 100)
SELECT 12 + x, 4 + ((x - 1) / 5),
       printf('REAL-%02d-%02d', 4 + ((x - 1) / 5), 1 + ((x - 1) % 5)),
       CASE WHEN x % 2 = 0 THEN 'SLOW' ELSE 'FAST' END,
       CASE WHEN x % 2 = 0 THEN 7.0 ELSE 60.0 END,
       CASE WHEN x BETWEEN 1 AND 10 THEN 'RESERVED'
            WHEN x BETWEEN 11 AND 20 THEN 'CHARGING'
            WHEN x % 17 = 0 THEN 'FAULT'
            WHEN x % 13 = 0 THEN 'OFFLINE'
            ELSE 'IDLE' END
FROM n;

INSERT OR IGNORE INTO charging_orders
    (order_id, order_no, user_id, pile_id, mode, status,
     reserved_at, started_at, ended_at, paid_at,
     duration_seconds, energy_wh, unit_price_cents_per_kwh,
     amount_cents, created_at)
WITH RECURSIVE n(x) AS (SELECT 1 UNION ALL SELECT x + 1 FROM n WHERE x < 200)
SELECT
    1100 + x,
    printf('REAL-ORDER-%04d', x),
    CASE WHEN x BETWEEN 181 AND 190 THEN 6 + (x - 181)
         WHEN x BETWEEN 191 AND 200 THEN 16 + (x - 191)
         ELSE 5 + ((x - 1) % 20) END,
    CASE WHEN x BETWEEN 181 AND 190 THEN 42 + (x - 181)
         WHEN x BETWEEN 191 AND 200 THEN 52 + (x - 191)
         ELSE 12 + (x - 1) % 80 END,
    CASE WHEN x BETWEEN 171 AND 190 THEN 'RESERVATION'
         WHEN x % 2 = 0 THEN 'DIRECT' ELSE 'RESERVATION' END,
    'COMPLETED',
    CASE WHEN (x % 2 = 0 AND NOT (x BETWEEN 171 AND 190)) THEN NULL
         ELSE strftime('%Y-%m-%dT%H:%M:%SZ','now',printf('-%d days', x % 45 + 1),'-35 minutes') END,
    strftime('%Y-%m-%dT%H:%M:%SZ','now',printf('-%d days', x % 45 + 1),'-30 minutes'),
    strftime('%Y-%m-%dT%H:%M:%SZ','now',printf('-%d days', x % 45 + 1),'+30 minutes'),
    strftime('%Y-%m-%dT%H:%M:%SZ','now',printf('-%d days', x % 45 + 1),'+30 minutes'),
    3600,
    5000 + (x % 8) * 1000,
    CASE WHEN x % 3 = 0 THEN 135 ELSE 145 END,
    (((5000 + (x % 8) * 1000) * CASE WHEN x % 3 = 0 THEN 135 ELSE 145 END + 500) / 1000),
    strftime('%Y-%m-%dT%H:%M:%SZ','now',printf('-%d days', x % 45 + 1),'-50 minutes')
FROM n;

COMMIT;
