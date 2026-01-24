"""clickstream_03_fill_analytics_marts

Назначение:
  Заполняет витрины в БД `analytics` на основе подготовленных (дедуп+нормализация) событий.

Вход:
  - clickstream.prepared_events   (DAG 02)
  - clickstream.invalid_events    (для метрик качества)
  - clickstream.etl_state         (watermark)

Выход (витрины):
  - analytics.kpi_daily
  - analytics.pages_daily
  - analytics.session_quality_daily
  - analytics.referrer_daily
  - analytics.user_cohorts_daily
"""

from __future__ import annotations

from datetime import datetime, timedelta
import os

from airflow import DAG
from airflow.operators.python import PythonOperator


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


def ensure_state():
    """Гарантируем, что watermark для DAG3 есть."""
    ch_execute(
        """
        INSERT INTO clickstream.etl_state (pipeline, last_ts)
        SELECT 'fill_analytics_marts', toDateTime64('1970-01-01 00:00:00', 3)
        WHERE NOT EXISTS (
          SELECT 1 FROM clickstream.etl_state WHERE pipeline='fill_analytics_marts'
        )
        """
    )


def fill_analytics_marts():
    safety_delay_sec = 10

    last_ts = ch_query(
        "SELECT last_ts FROM clickstream.etl_state WHERE pipeline='fill_analytics_marts' LIMIT 1"
    )[0][0]
    upper_ts = ch_query(f"SELECT now64(3) - INTERVAL {int(safety_delay_sec)} SECOND")[0][0]

    full_days_only = (os.getenv("CLICKSTREAM_MARTS_FULL_DAYS", "0") == "1")
    end_day = ch_query(
        "SELECT addDays(today(), -1)" if full_days_only else "SELECT today()"
    )[0][0]

    backfill_days = int(os.getenv("CLICKSTREAM_MARTS_BACKFILL_DAYS", "120"))
    start_day_candidate = ch_query(
        f"SELECT addDays(toDate({repr(str(end_day))}), -{backfill_days - 1})"
    )[0][0]

    min_day_prepared = ch_query(
        "SELECT min(toDate(event_time)) FROM clickstream.prepared_events"
    )[0][0]
    if min_day_prepared is None:
        print("[fill_analytics_marts] prepared_events is empty: nothing to do")
        ch_execute(
            f"""
            ALTER TABLE clickstream.etl_state
            UPDATE last_ts = toDateTime64({repr(str(upper_ts))}, 3)
            WHERE pipeline='fill_analytics_marts'
            """
        )
        return

    start_day = max(start_day_candidate, min_day_prepared)

    print(
        f"[fill_analytics_marts] window_days={backfill_days} full_days_only={full_days_only} "
        f"range=[{start_day}..{end_day}] min_prepared_day={min_day_prepared} last_ts={last_ts}"
    )

    if start_day > end_day:
        print("[fill_analytics_marts] nothing to do (start_day > end_day)")
        ch_execute(
            f"""
            ALTER TABLE clickstream.etl_state
            UPDATE last_ts = toDateTime64({repr(str(upper_ts))}, 3)
            WHERE pipeline='fill_analytics_marts'
            """
        )
        return

    # --- 1) KPI DAILY ---
    kpi_sql = f"""
    INSERT INTO analytics.kpi_daily
    SELECT
      day,
      events,
      users,
      sessions,
      views,
      clicks,
      purchases,
      if(views = 0, 0.0, clicks / views) AS ctr,
      if(views = 0, 0.0, purchases / views) AS conversion,
      invalid_events,
      if(events + invalid_events = 0, 0.0, invalid_events / (events + invalid_events)) AS invalid_share,
      if(sessions = 0, 0.0, events / sessions) AS avg_events_per_session,
      now() AS calculated_at
    FROM
    (
      SELECT
        toDate(event_time) AS day,
        count() AS events,
        uniqExact(user_id) AS users,
        uniqExact(session_id) AS sessions,
        countIf(event_type = 'view') AS views,
        countIf(event_type = 'click') AS clicks,
        countIf(event_type = 'purchase') AS purchases
      FROM clickstream.prepared_events
      WHERE toDate(event_time) BETWEEN toDate({repr(str(start_day))}) AND toDate({repr(str(end_day))})
      GROUP BY day
    ) pe
    LEFT JOIN
    (
      SELECT
        toDate(event_time) AS day,
        count() AS invalid_events
      FROM clickstream.invalid_events
      WHERE toDate(event_time) BETWEEN toDate({repr(str(start_day))}) AND toDate({repr(str(end_day))})
      GROUP BY day
    ) ie USING (day)
    """
    ch_execute(kpi_sql, settings={"max_execution_time": 300})

    # --- 2) PAGES DAILY ---
    pages_sql = f"""
    INSERT INTO analytics.pages_daily
    SELECT
      toDate(event_time) AS day,
      page_path AS page_url,
      countIf(event_type = 'view') AS views,
      countIf(event_type = 'click') AS clicks,
      uniqExact(user_id) AS users,
      uniqExact(session_id) AS sessions,
      if(views = 0, 0.0, clicks / views) AS ctr,
      avg(lengthUTF8(props_json)) AS avg_props_size,
      now() AS calculated_at
    FROM clickstream.prepared_events
    WHERE toDate(event_time) BETWEEN toDate({repr(str(start_day))}) AND toDate({repr(str(end_day))})
    GROUP BY day, page_url
    """
    ch_execute(pages_sql, settings={"max_execution_time": 300})

    # --- 3) SESSION QUALITY DAILY ---
    session_q_sql = f"""
    INSERT INTO analytics.session_quality_daily
    SELECT
      day,
      count() AS sessions,
      avg(events_per_session) AS avg_events_per_session,
      quantileExact(0.5)(events_per_session) AS median_events_per_session,
      avg(duration_sec) AS avg_duration_sec,
      quantileExact(0.5)(duration_sec) AS median_duration_sec,
      if(sessions = 0, 0.0, countIf(events_per_session = 1) / sessions) AS bounce_rate,
      now() AS calculated_at
    FROM
    (
      SELECT
        toDate(min(event_time)) AS day,
        session_id,
        count() AS events_per_session,
        dateDiff('second', min(event_time), max(event_time)) AS duration_sec
      FROM clickstream.prepared_events
      WHERE toDate(event_time) BETWEEN toDate({repr(str(start_day))}) AND toDate({repr(str(end_day))})
      GROUP BY session_id
    ) s
    GROUP BY day
    """
    ch_execute(session_q_sql, settings={"max_execution_time": 300})

    # --- 4) REFERRER DAILY ---
    ref_sql = f"""
    INSERT INTO analytics.referrer_daily
    SELECT
      day,
      referrer,
      events,
      users,
      sessions,
      views,
      clicks,
      if(views = 0, 0.0, clicks / views) AS ctr,
      if(total_events = 0, 0.0, events / total_events) AS traffic_share,
      now() AS calculated_at
    FROM
    (
      SELECT
        toDate(event_time) AS day,
        if(referrer = '' OR referrer IS NULL, '(direct)', referrer) AS referrer,
        count() AS events,
        uniqExact(user_id) AS users,
        uniqExact(session_id) AS sessions,
        countIf(event_type = 'view') AS views,
        countIf(event_type = 'click') AS clicks
      FROM clickstream.prepared_events
      WHERE toDate(event_time) BETWEEN toDate({repr(str(start_day))}) AND toDate({repr(str(end_day))})
      GROUP BY day, referrer
    ) r
    INNER JOIN
    (
      SELECT
        toDate(event_time) AS day,
        count() AS total_events
      FROM clickstream.prepared_events
      WHERE toDate(event_time) BETWEEN toDate({repr(str(start_day))}) AND toDate({repr(str(end_day))})
      GROUP BY day
    ) t USING (day)
    """
    ch_execute(ref_sql, settings={"max_execution_time": 300})

    # --- 5) USER COHORTS DAILY ---
    # сегментация по активности: light=1, medium=2..4, heavy>=5
    cohorts_sql = f"""
    INSERT INTO analytics.user_cohorts_daily
    WITH
      user_first AS (
        SELECT user_id, min(toDate(event_time)) AS first_day
        FROM clickstream.prepared_events
        GROUP BY user_id
      ),
      per_user_day AS (
        SELECT
          toDate(event_time) AS day,
          user_id,
          count() AS events_per_user
        FROM clickstream.prepared_events
        WHERE toDate(event_time) BETWEEN toDate({repr(str(start_day))}) AND toDate({repr(str(end_day))})
        GROUP BY day, user_id
      )
    SELECT
      d.day AS day,
      countIf(f.first_day = d.day) AS new_users,
      (uniqExact(d.user_id) - countIf(f.first_day = d.day)) AS returning_users,
      if(uniqExact(d.user_id) = 0, 0.0, countIf(f.first_day = d.day) / uniqExact(d.user_id)) AS new_share,
      countIf(d.events_per_user >= 5) AS heavy_users,
      countIf(d.events_per_user BETWEEN 2 AND 4) AS medium_users,
      countIf(d.events_per_user = 1) AS light_users,
      now() AS calculated_at
    FROM per_user_day d
    LEFT JOIN user_first f USING (user_id)
    GROUP BY day
    """
    ch_execute(cohorts_sql, settings={"max_execution_time": 300})

    # watermark
    ch_execute(
        f"""
        ALTER TABLE clickstream.etl_state
        UPDATE last_ts = toDateTime64({repr(str(upper_ts))}, 3)
        WHERE pipeline='fill_analytics_marts'
        """
    )


default_args = {
    "owner": "clickstream",
    "retries": 2,
    "retry_delay": timedelta(seconds=30),
}


with DAG(
    dag_id="clickstream_03_fill_analytics_marts",
    start_date=datetime(2025, 1, 1),
    schedule="15 1 * * *",  # ежедневно в 01:15
    catchup=False,
    default_args=default_args,
    tags=["clickstream", "analytics", "marts"],
) as dag:
    t0 = PythonOperator(task_id="ensure_state", python_callable=ensure_state)
    t1 = PythonOperator(task_id="fill_analytics_marts", python_callable=fill_analytics_marts)

    t0 >> t1
