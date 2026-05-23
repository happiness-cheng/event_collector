-- ClickHouse 建表语句
-- 在 ClickHouse 容器初始化时自动执行

CREATE TABLE IF NOT EXISTS events (
    event_id   String,
    user_id    String,
    event_type String,
    platform   String,
    ts         UInt64,
    payload    String
) ENGINE = MergeTree()
ORDER BY (event_type, ts)
SETTINGS index_granularity = 8192;
