-- =============================================================================
-- Clickstream DWH в ClickHouse
-- =============================================================================

-- =============================================================================
-- 0) Базы данных
-- =============================================================================
CREATE DATABASE IF NOT EXISTS clickstream ON CLUSTER clickstream_cluster;
CREATE DATABASE IF NOT EXISTS analytics  ON CLUSTER clickstream_cluster;

-- =============================================================================
-- 1) RAW: единая таблица для HTTP / Kafka / CSV
-- =============================================================================
CREATE TABLE IF NOT EXISTS clickstream.raw_events ON CLUSTER clickstream_cluster
(
    -- Идентификатор и время события
    event_id UUID DEFAULT generateUUIDv4(),
    event_time DateTime64(3),
    received_time DateTime64(3) DEFAULT now64(3),

    -- Ключевые поля
    user_id String,
    session_id String,
    device_id String,

    -- Тип и контекст
    event_type LowCardinality(String),
    page_url String,
    referrer String,
    user_agent String,
    ip String,

    -- Полезная нагрузка (в JSON-строке)
    props_json String,

    -- Откуда пришло событие: http | kafka | csv
    source LowCardinality(String) DEFAULT 'http',

    -- Метаданные Kafka
    source_topic LowCardinality(String) DEFAULT '',
    source_partition Int32 DEFAULT -1,
    source_offset Int64 DEFAULT -1,

    -- Метаданные CSV/MinIO
    source_object String DEFAULT '',
    source_row_number UInt64 DEFAULT 0,

    -- Исходный payload
    raw_payload String DEFAULT '',

    -- Идентификатор ETL-запуска (например Airflow run_id)
    ingest_run_id String DEFAULT ''
)
ENGINE = ReplicatedMergeTree('/clickhouse/tables/{shard}/clickstream/raw_events', '{replica}')
PARTITION BY toYYYYMM(event_time)
ORDER BY (event_time, user_id, session_id)
SETTINGS index_granularity = 8192;

-- RAW не должен расти бесконечно
ALTER TABLE clickstream.raw_events ON CLUSTER clickstream_cluster
MODIFY TTL toDateTime(event_time) + INTERVAL 30 DAY DELETE;

-- =============================================================================
-- 2) INVALID: единая таблица невалидных/битых событий для ВСЕХ источников
-- =============================================================================
CREATE TABLE IF NOT EXISTS clickstream.invalid_events ON CLUSTER clickstream_cluster
(
    received_time DateTime64(3) DEFAULT now64(3),

    event_id UUID,
    event_time DateTime64(3),

    user_id String,
    session_id String,
    device_id String,
    event_type String,
    page_url String,
    referrer String,
    user_agent String,
    ip String,
    props_json String,

    source LowCardinality(String),
    source_topic String,
    source_partition Int32,
    source_offset Int64,
    source_object String,
    source_row_number UInt64,
    ingest_run_id String,

    raw_payload String,

    -- Причина
    error_code LowCardinality(String),
    error_message String
)
ENGINE = ReplicatedMergeTree('/clickhouse/tables/{shard}/clickstream/invalid_events', '{replica}')
PARTITION BY toYYYYMM(received_time)
ORDER BY (received_time, error_code);

ALTER TABLE clickstream.invalid_events ON CLUSTER clickstream_cluster
MODIFY TTL toDateTime(received_time) + INTERVAL 30 DAY DELETE;

-- =============================================================================
-- 3) VALID: валидные события (после проверки из RAW)
-- =============================================================================

CREATE TABLE IF NOT EXISTS clickstream.valid_events ON CLUSTER clickstream_cluster
(
    event_id UUID,
    event_time DateTime64(3),

    user_id String,
    session_id String,
    device_id String,

    event_type LowCardinality(String),
    page_url String,
    referrer String,

    user_agent String,
    ip String,

    props_json String,

    source LowCardinality(String),
    source_topic LowCardinality(String),
    source_partition Int32,
    source_offset Int64,
    source_object String,
    source_row_number UInt64,

    cleaned_time DateTime64(3) DEFAULT now64(3),
    ingest_run_id String
)
ENGINE = ReplicatedMergeTree('/clickhouse/tables/{shard}/clickstream/valid_events', '{replica}')
PARTITION BY toYYYYMM(event_time)
ORDER BY (event_time, user_id, session_id);

-- =============================================================================
-- 4) Единая валидация RAW -> VALID / INVALID (для HTTP/Kafka/CSV)
-- =============================================================================

CREATE MATERIALIZED VIEW IF NOT EXISTS clickstream.mv_raw_to_valid
ON CLUSTER clickstream_cluster
TO clickstream.valid_events
AS
SELECT
    event_id,
    event_time,
    user_id,
    session_id,
    device_id,
    event_type,
    page_url,
    referrer,
    user_agent,
    ip,
    props_json,

    source,
    source_topic,
    source_partition,
    source_offset,
    source_object,
    source_row_number,

    now64(3) AS cleaned_time,
    ingest_run_id
FROM clickstream.raw_events
WHERE
    user_id != ''
    AND session_id != ''
    AND page_url != ''
    AND event_type IN ('click','view','purchase','signup','login','logout');

CREATE MATERIALIZED VIEW IF NOT EXISTS clickstream.mv_raw_to_invalid
ON CLUSTER clickstream_cluster
TO clickstream.invalid_events
AS
SELECT
    now64(3) AS received_time,

    event_id,
    event_time,
    user_id,
    session_id,
    device_id,
    toString(event_type) AS event_type,
    page_url,
    referrer,
    user_agent,
    ip,
    props_json,

    source,
    toString(source_topic) AS source_topic,
    source_partition,
    source_offset,
    source_object,
    source_row_number,
    ingest_run_id,

    raw_payload,

    'VALIDATION_FAILED' AS error_code,
    concat(
        if(user_id = '', 'missing_user_id; ', ''),
        if(session_id = '', 'missing_session_id; ', ''),
        if(page_url = '', 'missing_page_url; ', ''),
        if(NOT (event_type IN ('click','view','purchase','signup','login','logout')), 'bad_event_type; ', '')
    ) AS error_message
FROM clickstream.raw_events
WHERE NOT (
    user_id != ''
    AND session_id != ''
    AND page_url != ''
    AND event_type IN ('click','view','purchase','signup','login','logout')
);

-- =============================================================================
-- 5) Kafka ingestion (минимум объектов)
--    1 Kafka-table -> 1 MV -> raw_events (source='kafka')
--    Дальше валид/невалид обработается общими MV выше.
-- =============================================================================

CREATE TABLE IF NOT EXISTS clickstream.raw_events_kafka
ON CLUSTER clickstream_cluster
(
    raw_payload String
)
ENGINE = Kafka
SETTINGS
    kafka_broker_list = 'kafka-1:9092,kafka-2:9092,kafka-3:9092',
    kafka_topic_list = 'clickstream-events-raw',
    kafka_group_name = 'ch_raw_ingest',
    -- Важно: генератор пишет в Kafka *само событие* (JSON), например {"type":"click",...}
    -- Поэтому нам нужно прочитать сообщение целиком в строку raw_payload.
    -- JSONEachRow ожидает JSON с ключом raw_payload и оставляет колонку пустой.
    kafka_format = 'JSONAsString',
    kafka_num_consumers = 1,
    kafka_handle_error_mode = 'stream';

CREATE MATERIALIZED VIEW IF NOT EXISTS clickstream.mv_kafka_to_raw
ON CLUSTER clickstream_cluster
TO clickstream.raw_events
AS
SELECT
  coalesce(parseDateTime64BestEffortOrNull(JSONExtractString(raw_payload, 'created_at'), 3), now64(3)) AS event_time,
  coalesce(JSONExtractString(raw_payload, 'user_id'), toString(JSONExtractInt(raw_payload, 'user_id')), '') AS user_id,
  JSONExtractString(raw_payload, 'session_id') AS session_id,
  JSONExtractString(raw_payload, 'device_id') AS device_id,
  JSONExtractString(raw_payload, 'type') AS event_type,
  JSONExtractString(raw_payload, 'url') AS page_url,
  JSONExtractString(raw_payload, 'referrer') AS referrer,
  JSONExtractString(raw_payload, 'user_agent') AS user_agent,
  JSONExtractString(raw_payload, 'ip') AS ip,

  coalesce(
    nullIf(JSONExtractRaw(raw_payload, 'props'), ''),
    nullIf(
      replaceAll(
        replaceAll(JSONExtractString(raw_payload, 'props_json'), '\\\\\"', '\"'),
        '\\"', '\"'
      ),
      ''
    ),
    '{}'
  ) AS props_json,

  'kafka' AS source,
  'clickstream-events-raw' AS source_topic,
  _partition AS source_partition,
  _offset AS source_offset,
  '' AS source_object,
  toUInt64(0) AS source_row_number,
  raw_payload AS raw_payload,
  '' AS ingest_run_id
FROM clickstream.raw_events_kafka;
-- =============================================================================
-- 5.1) ETL STATE + PREPARED / AGG TABLES (используются DAG 02 и последующими)
-- =============================================================================

-- Watermark / состояние пайплайнов
CREATE TABLE IF NOT EXISTS clickstream.etl_state ON CLUSTER clickstream_cluster
(
  pipeline LowCardinality(String),
  last_ts DateTime64(3)
)
ENGINE = ReplicatedMergeTree('/clickhouse/tables/{shard}/clickstream/etl_state', '{replica}')
ORDER BY pipeline;

-- Дедуплицированные и «подготовленные» события (нормализация URL + извлечение ключей из props_json)
CREATE TABLE IF NOT EXISTS clickstream.prepared_events ON CLUSTER clickstream_cluster
(
  dedup_key String,
  prepared_time DateTime64(3),

  event_id UUID,
  event_time DateTime64(3),
  cleaned_time DateTime64(3),

  user_id String,
  session_id String,
  device_id String,

  event_type LowCardinality(String),
  page_url String,
  page_path String,
  page_query String,

  referrer String,
  user_agent String,
  ip String,

  props_json String,
  props_event_title LowCardinality(String),
  props_element_id LowCardinality(String),
  props_x Int32,
  props_y Int32,

  source LowCardinality(String),
  source_topic LowCardinality(String),
  source_partition Int32,
  source_offset Int64,
  source_object String,
  source_row_number UInt64,
  ingest_run_id String
)
ENGINE = ReplicatedReplacingMergeTree('/clickhouse/tables/{shard}/clickstream/prepared_events', '{replica}', prepared_time)
PARTITION BY toYYYYMM(event_time)
ORDER BY (dedup_key);

-- Пример агрегатов по сессиям в 5-минутных окнах (можно расширять)
CREATE TABLE IF NOT EXISTS clickstream.session_metrics_5m ON CLUSTER clickstream_cluster
(
  window_start DateTime,
  session_id String,
  user_id String,

  events_count UInt64,
  clicks UInt64,
  views UInt64,
  purchases UInt64,
  logins UInt64,
  signups UInt64,
  logouts UInt64,

  uniq_pages UInt64
)
ENGINE = ReplicatedSummingMergeTree('/clickhouse/tables/{shard}/clickstream/session_metrics_5m', '{replica}')
PARTITION BY toYYYYMM(window_start)
ORDER BY (window_start, session_id, user_id);

-- =============================================================================
-- 6) ANALYTICS
-- =============================================================================

-- 6.1 KPI по дням + качество данных
CREATE TABLE IF NOT EXISTS analytics.kpi_daily
ON CLUSTER clickstream_cluster
(
    day Date,
    events UInt64,
    users UInt64,
    sessions UInt64,
    views UInt64,
    clicks UInt64,
    purchases UInt64,
    ctr Float32,
    conversion Float32,
    invalid_events UInt64,
    invalid_share Float32,
    avg_events_per_session Float32,
    calculated_at DateTime DEFAULT now()
)
ENGINE = ReplicatedReplacingMergeTree('/clickhouse/tables/{shard}/analytics/kpi_daily', '{replica}', calculated_at)
PARTITION BY toYYYYMM(day)
ORDER BY (day);

-- 6.2 Страницы по дням: просмотры/клики/CTR
CREATE TABLE IF NOT EXISTS analytics.pages_daily
ON CLUSTER clickstream_cluster
(
    day Date,
    page_url String,
    views UInt64,
    clicks UInt64,
    users UInt64,
    sessions UInt64,
    ctr Float32,
    avg_props_size Float32,
    calculated_at DateTime DEFAULT now()
)
ENGINE = ReplicatedReplacingMergeTree('/clickhouse/tables/{shard}/analytics/pages_daily', '{replica}', calculated_at)
PARTITION BY toYYYYMM(day)
ORDER BY (day, page_url);

-- 6.3 Качество сессий: длительность/батчи/отказы (bounce)
CREATE TABLE IF NOT EXISTS analytics.session_quality_daily
ON CLUSTER clickstream_cluster
(
    day Date,
    sessions UInt64,
    avg_events_per_session Float32,
    median_events_per_session Float32,
    avg_duration_sec Float32,
    median_duration_sec Float32,
    bounce_rate Float32,
    calculated_at DateTime DEFAULT now()
)
ENGINE = ReplicatedReplacingMergeTree('/clickhouse/tables/{shard}/analytics/session_quality_daily', '{replica}', calculated_at)
PARTITION BY toYYYYMM(day)
ORDER BY (day);

-- 6.4 Рефереры: доля трафика + CTR
CREATE TABLE IF NOT EXISTS analytics.referrer_daily
ON CLUSTER clickstream_cluster
(
    day Date,
    referrer String,
    events UInt64,
    users UInt64,
    sessions UInt64,
    views UInt64,
    clicks UInt64,
    ctr Float32,
    traffic_share Float32,
    calculated_at DateTime DEFAULT now()
)
ENGINE = ReplicatedReplacingMergeTree('/clickhouse/tables/{shard}/analytics/referrer_daily', '{replica}', calculated_at)
PARTITION BY toYYYYMM(day)
ORDER BY (day, referrer);

-- 6.5 Пользователи: новые/возвратные + сегменты активности
CREATE TABLE IF NOT EXISTS analytics.user_cohorts_daily
ON CLUSTER clickstream_cluster
(
    day Date,
    new_users UInt64,
    returning_users UInt64,
    new_share Float32,
    heavy_users UInt64,
    medium_users UInt64,
    light_users UInt64,
    calculated_at DateTime DEFAULT now()
)
ENGINE = ReplicatedReplacingMergeTree('/clickhouse/tables/{shard}/analytics/user_cohorts_daily', '{replica}', calculated_at)
PARTITION BY toYYYYMM(day)
ORDER BY (day);
