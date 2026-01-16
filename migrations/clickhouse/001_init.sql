-- =============================================================================
-- База (DWH) и слои RAW/DDS/DM
-- =============================================================================
CREATE DATABASE IF NOT EXISTS clickstream ON CLUSTER clickstream_cluster;

-- =============================================================================
-- RAW слой
-- =============================================================================
CREATE TABLE IF NOT EXISTS clickstream.raw_events ON CLUSTER clickstream_cluster
(
    event_id UUID DEFAULT generateUUIDv4(),
    event_time DateTime64(3),
    received_time DateTime64(3) DEFAULT now64(3),

    user_id String,
    session_id String,
    device_id String,

    event_type LowCardinality(String),
    page_url String,
    referrer String,
    user_agent String,
    ip String,

    props_json String
)
ENGINE = ReplicatedMergeTree('/clickhouse/tables/{shard}/clickstream/raw_events', '{replica}')
PARTITION BY toYYYYMM(event_time)
ORDER BY (event_time, user_id, session_id);

-- =============================================================================
-- DDS слой: обработанные/нормализованные данные
-- =============================================================================
CREATE TABLE IF NOT EXISTS clickstream.dds_events ON CLUSTER clickstream_cluster
(
    event_id UUID,
    event_time DateTime64(3),

    user_id String,
    session_id String,
    event_type LowCardinality(String),

    page_url String,
    referrer String,

    props_json String
)
ENGINE = ReplicatedMergeTree('/clickhouse/tables/{shard}/clickstream/dds_events', '{replica}')
PARTITION BY toYYYYMM(event_time)
ORDER BY (event_time, user_id, session_id);

-- =============================================================================
-- DM слой: пример ежедневной активности
-- =============================================================================
CREATE TABLE IF NOT EXISTS clickstream.dm_daily_active_users ON CLUSTER clickstream_cluster
(
    day Date,
    dau UInt64
)
ENGINE = ReplicatedSummingMergeTree('/clickhouse/tables/{shard}/clickstream/dm_daily_active_users', '{replica}')
PARTITION BY toYYYYMM(day)
ORDER BY (day);
