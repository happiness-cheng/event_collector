-- ClickHouse 建表语句
-- 在 ClickHouse 容器初始化时自动执行

CREATE TABLE IF NOT EXISTS events (
    payload String
) ENGINE = MergeTree()
ORDER BY tuple()
SETTINGS index_granularity = 8192;
