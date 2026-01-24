"""clickstream_02_dedup_prepare_aggregate

Вход:  clickstream.valid_events  (валидные события после MV raw->valid)
Выход:
  - clickstream.prepared_events        (дедуп + лёгкая нормализация, готово к витринам)
  - clickstream.session_metrics_5m     (пример агрегатов по сессиям в 5-мин окна)

Почему так:
  * Валидация/маршрутизация уже делается ClickHouse Materialized View (raw -> valid/invalid).
  * DAG делает то, что MV делать неудобно/дорого: дедуп, разбор URL, извлечение полей из props_json,
    инкрементальные агрегаты.
"""

from __future__ import annotations

from datetime import datetime, timedelta
import os

from airflow import DAG
from airflow.operators.python import PythonOperator


EVENT_TYPES = ("click", "view", "purchase", "signup", "login", "logout")


def _get_client():
    """Create ClickHouse client with basic failover."""
    from clickhouse_driver import Client

    hosts_raw = os.getenv("CLICKHOUSE_HOSTS") or os.getenv("CLICKHOUSE_HOST") or "clickhouse-1"
    hosts = [h.strip() for h in hosts_raw.split(",") if h.strip()]

    port = int(os.getenv("CLICKHOUSE_PORT", "9000"))
    user = os.getenv("CLICKHOUSE_USER", "clickstream")
    password = os.getenv("CLICKHOUSE_PASSWORD", "")
    database = os.getenv("CLICKHOUSE_DB", "clickstream")

    last_exc = None
    for host in hosts:
        try:
            c = Client(
                host=host,
                port=port,
                user=user,
                password=password,
                database=database,
                send_receive_timeout=300,
                connect_timeout=5,
            )
            c.execute("SELECT 1")
            return c
        except Exception as e:
            last_exc = e
    raise RuntimeError(f"Cannot connect to ClickHouse hosts={hosts}. Last error: {last_exc!r}")


def ch_execute(sql: str, settings: dict | None = None) -> None:
    if settings:
        _get_client().execute(sql, settings=settings)
    else:
        _get_client().execute(sql)


def ch_query(sql: str):
    return _get_client().execute(sql)


def ensure_tables():
    # watermark state
    ch_execute(
        """
        CREATE TABLE IF NOT EXISTS clickstream.etl_state
        (
          pipeline LowCardinality(String),
          last_ts DateTime64(3)
        )
        ENGINE = MergeTree
        ORDER BY pipeline
        """
    )
    ch_execute(
        """
        INSERT INTO clickstream.etl_state (pipeline, last_ts)
        SELECT 'dedup_prepare_agg', toDateTime64('1970-01-01 00:00:00', 3)
        WHERE NOT EXISTS (SELECT 1 FROM clickstream.etl_state WHERE pipeline='dedup_prepare_agg')
        """
    )

    # prepared events: дедуп через ReplacingMergeTree по dedup_key
    ch_execute(
        """
        CREATE TABLE IF NOT EXISTS clickstream.prepared_events
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
        ENGINE = ReplacingMergeTree(prepared_time)
        ORDER BY (dedup_key)
        """
    )

    # пример агрегатов (можно будет расширять под витрины)
    ch_execute(
        """
        CREATE TABLE IF NOT EXISTS clickstream.session_metrics_5m
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
        ENGINE = SummingMergeTree
        ORDER BY (window_start, session_id, user_id)
        """
    )


def dedup_prepare_aggregate():
    """Incremental: (last_ts, upper_ts] by cleaned_time from valid_events."""
    safety_delay_sec = 10

    last_ts = ch_query(
        "SELECT last_ts FROM clickstream.etl_state WHERE pipeline='dedup_prepare_agg' LIMIT 1"
    )[0][0]
    upper_ts = ch_query(f"SELECT now64(3) - INTERVAL {int(safety_delay_sec)} SECOND")[0][0]

    print(f"[dedup_prepare_agg] window: ({last_ts}, {upper_ts}]")

    # 1) prepared_events from valid_events
    # dedup_key:
    #  - kafka: topic:partition:offset
    #  - csv:   source_object:source_row_number
    #  - http/other: event_id
    prepared_sql = f"""
    INSERT INTO clickstream.prepared_events
    SELECT
      multiIf(
        source = 'kafka',
          concat(toString(source_topic), ':', toString(source_partition), ':', toString(source_offset)),
        source = 'csv',
          concat(source_object, ':', toString(source_row_number)),
        toString(event_id)
      ) AS dedup_key,

      now64(3) AS prepared_time,

      event_id,
      event_time,
      cleaned_time,

      user_id,
      session_id,
      device_id,

      toLowCardinality(event_type) AS event_type,

      page_url,
      if(position(page_url, '?') > 0, substring(page_url, 1, position(page_url, '?') - 1), page_url) AS page_path,
      if(position(page_url, '?') > 0, substring(page_url, position(page_url, '?') + 1), '') AS page_query,

      referrer,
      user_agent,
      ip,

      props_json,
      toLowCardinality(coalesce(JSONExtractString(props_json, 'event_title'), '')) AS props_event_title,
      toLowCardinality(coalesce(JSONExtractString(props_json, 'element_id'), '')) AS props_element_id,
      toInt32OrZero(toString(JSONExtractInt(props_json, 'x'))) AS props_x,
      toInt32OrZero(toString(JSONExtractInt(props_json, 'y'))) AS props_y,

      source,
      toLowCardinality(source_topic) AS source_topic,
      toInt32(source_partition) AS source_partition,
      toInt64(source_offset) AS source_offset,
      source_object,
      toUInt64(source_row_number) AS source_row_number,
      ingest_run_id
    FROM clickstream.valid_events
    WHERE cleaned_time > toDateTime64({repr(str(last_ts))}, 3)
      AND cleaned_time <= toDateTime64({repr(str(upper_ts))}, 3)
      AND event_type IN {EVENT_TYPES}
    """
    ch_execute(prepared_sql, settings={"max_execution_time": 300})

    # 2) aggregates (5-min windows)
    agg_sql = f"""
    INSERT INTO clickstream.session_metrics_5m
    SELECT
      toStartOfFiveMinute(toDateTime(event_time)) AS window_start,
      session_id,
      user_id,

      count() AS events_count,
      countIf(event_type = 'click') AS clicks,
      countIf(event_type = 'view') AS views,
      countIf(event_type = 'purchase') AS purchases,
      countIf(event_type = 'login') AS logins,
      countIf(event_type = 'signup') AS signups,
      countIf(event_type = 'logout') AS logouts,
      uniqExact(page_path) AS uniq_pages
    FROM clickstream.prepared_events
    WHERE prepared_time > toDateTime64({repr(str(last_ts))}, 3)
      AND prepared_time <= toDateTime64({repr(str(upper_ts))}, 3)
    GROUP BY window_start, session_id, user_id
    """
    ch_execute(agg_sql, settings={"max_execution_time": 300})

    # 3) update watermark
    ch_execute(
        f"""
        ALTER TABLE clickstream.etl_state
        UPDATE last_ts = toDateTime64({repr(str(upper_ts))}, 3)
        WHERE pipeline='dedup_prepare_agg'
        """
    )


default_args = {
    "owner": "clickstream",
    "retries": 2,
    "retry_delay": timedelta(seconds=30),
}


with DAG(
    dag_id="clickstream_02_dedup_prepare_aggregate",
    start_date=datetime(2025, 1, 1),
    schedule="*/2 * * * *",
    catchup=False,
    default_args=default_args,
    tags=["clickstream", "valid", "dedup", "aggregate"],
) as dag:
    t0 = PythonOperator(task_id="ensure_tables", python_callable=ensure_tables)
    t1 = PythonOperator(task_id="dedup_prepare_aggregate", python_callable=dedup_prepare_aggregate)

    t0 >> t1
