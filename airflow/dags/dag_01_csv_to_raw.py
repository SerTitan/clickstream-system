"""DAG 2: MinIO CSV (contract) -> RAW
DAG забирает CSV из MinIO и складывает сырые данные в ClickHouse (clickstream.raw_events).
"""

from __future__ import annotations

from datetime import datetime, timedelta
import os

from airflow import DAG
from airflow.operators.python import PythonOperator


# ---------------- ClickHouse helpers ----------------

def _get_client():
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
            client = Client(
                host=host,
                port=port,
                user=user,
                password=password,
                database=database,
                send_receive_timeout=300,
                connect_timeout=5,
            )
            client.execute("SELECT 1")
            return client
        except Exception as e:
            last_exc = e

    raise RuntimeError(f"Cannot connect to ClickHouse hosts={hosts}. Last error: {last_exc!r}")


def ch_execute(sql: str, timeout: int | None = None) -> None:
    client = _get_client()
    settings = {}
    if timeout is not None:
        settings["max_execution_time"] = int(timeout)
    client.execute(sql, settings=settings if settings else None)


def ch_query(sql: str):
    client = _get_client()
    return client.execute(sql)


# ---------------- MinIO helpers ----------------

def ensure_ingested_table():
    ch_execute(
        """
        CREATE TABLE IF NOT EXISTS clickstream.ingested_objects
        (
          source LowCardinality(String),
          bucket String,
          object_key String,
          etag String,
          ingested_at DateTime DEFAULT now()
        )
        ENGINE = MergeTree
        ORDER BY (source, bucket, object_key)
        """
    )


def list_minio_csv_objects():
    import boto3

    endpoint = os.environ.get("MINIO_ENDPOINT", "http://minio:9000")
    access = os.environ.get("MINIO_ACCESS_KEY", "minioadmin")
    secret = os.environ.get("MINIO_SECRET_KEY", "minioadmin123")
    bucket = os.environ.get("MINIO_BUCKET", "click-analysis")

    prefix = os.environ.get("MINIO_PREFIX", "events_contract_v1_")

    s3 = boto3.client(
        "s3",
        endpoint_url=endpoint,
        aws_access_key_id=access,
        aws_secret_access_key=secret,
        region_name="us-east-1",
    )

    objs: list[tuple[str, str]] = []
    token = None
    while True:
        kwargs = {"Bucket": bucket, "Prefix": prefix, "MaxKeys": 1000}
        if token:
            kwargs["ContinuationToken"] = token

        resp = s3.list_objects_v2(**kwargs)
        for obj in resp.get("Contents", []) or []:
            key = obj["Key"]
            if key.endswith(".csv"):
                etag = (obj.get("ETag") or "").strip('"')
                objs.append((key, etag))

        if resp.get("IsTruncated"):
            token = resp.get("NextContinuationToken")
        else:
            break

    print(f"[CSV] found {len(objs)} contract csv objects in s3://{bucket}/{prefix}")
    return {"bucket": bucket, "objects": objs, "prefix": prefix}


def ingest_new_csvs(**context):
    data = context["ti"].xcom_pull(task_ids="list_minio", key="return_value")
    if not data:
        print("[CSV] nothing to ingest")
        return

    endpoint = os.environ.get("MINIO_ENDPOINT", "http://minio:9000")
    access = os.environ.get("MINIO_ACCESS_KEY", "minioadmin")
    secret = os.environ.get("MINIO_SECRET_KEY", "minioadmin123")

    bucket: str = data["bucket"]
    objects: list[tuple[str, str]] = data["objects"]

    existing = set(
        row[0] for row in ch_query(
            "SELECT object_key FROM clickstream.ingested_objects "
            f"WHERE source='csv' AND bucket={repr(bucket)}"
        )
    )

    to_load = [(k, etag) for (k, etag) in objects if k not in existing]
    print(f"[CSV] new objects to load: {len(to_load)}")

    for key, etag in to_load:
        s3_url = endpoint.rstrip("/") + "/" + bucket + "/" + key

        sql = f"""
        INSERT INTO clickstream.raw_events
        (
          event_id,
          event_time,
          received_time,
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
          source_object,
          source_row_number,
          raw_payload,
          ingest_run_id
        )
        SELECT
          coalesce(toUUIDOrNull(event_id), generateUUIDv4()) AS event_id,

          coalesce(parseDateTime64BestEffortOrNull(toString(created_at), 3), now64(3)) AS event_time,
          coalesce(parseDateTime64BestEffortOrNull(toString(received_at), 3), now64(3)) AS received_time,

          coalesce(toString(user_id), '') AS user_id,
          coalesce(toString(session_id), '') AS session_id,

          -- в контракте есть device_type, реального device_id пока нет:
          coalesce(toString(device_type), '') AS device_id,

          coalesce(toString(event_type), '') AS event_type,
          coalesce(toString(url), '') AS page_url,

          coalesce(toString(referrer), '') AS referrer,
          coalesce(toString(user_agent), '') AS user_agent,
          coalesce(toString(ip), '') AS ip,

          coalesce(toString(payload_json), '{{}}') AS props_json,

          'csv' AS source,
          {repr(key)} AS source_object,
          rowNumberInAllBlocks() AS source_row_number,

          -- raw_payload: удобно для дебага (собираем “как есть” в JSON-строку)
          concat(
            '{{',
              '"event_id":',        toJSONString(toString(event_id)), ',',
              '"event_type":',      toJSONString(toString(event_type)), ',',
              '"created_at":',      toJSONString(toString(created_at)), ',',
              '"received_at":',     toJSONString(toString(received_at)), ',',
              '"session_id":',      toJSONString(toString(session_id)), ',',
              '"user_id":',         toJSONString(toString(user_id)), ',',
              '"ip":',              toJSONString(toString(ip)), ',',
              '"url":',             toJSONString(toString(url)), ',',
              '"referrer":',        toJSONString(toString(referrer)), ',',
              '"device_type":',     toJSONString(toString(device_type)), ',',
              '"user_agent":',      toJSONString(toString(user_agent)), ',',
              '"element_id":',      toJSONString(toString(element_id)), ',',
              '"element_type":',    toJSONString(toString(element_type)), ',',
              '"element_text":',    toJSONString(toString(element_text)), ',',
              '"payload_json":',    toJSONString(coalesce(toString(payload_json), '{{}}')),
            '}}'
          ) AS raw_payload,

          {repr(context['run_id'])} AS ingest_run_id
        FROM s3(
          {repr(s3_url)},
          {repr(access)},
          {repr(secret)},
          'CSVWithNames'
        )
        """

        ch_execute(sql, timeout=300)

        ch_execute(
            "INSERT INTO clickstream.ingested_objects (source, bucket, object_key, etag) VALUES "
            f"('csv', {repr(bucket)}, {repr(key)}, {repr(etag)})"
        )
        print(f"[CSV] ingested: {key}")


def check_recent_csv_raw():
    rows = ch_query(
        """
        SELECT count(), max(event_time), max(received_time)
        FROM clickstream.raw_events
        WHERE source='csv' AND received_time > now() - INTERVAL 30 MINUTE
        """
    )
    count_, max_event_time, max_received_time = rows[0]
    print(f"[CHECK] csv rows last30m={count_}, max_event_time={max_event_time}, max_received_time={max_received_time}")


# ---------------- DAG ----------------

default_args = {
    "owner": "clickstream",
    "retries": 2,
    "retry_delay": timedelta(seconds=30),
}

with DAG(
    dag_id="clickstream_01_csv_to_raw",
    start_date=datetime(2025, 1, 1),
    schedule="*/5 * * * *",
    catchup=False,
    default_args=default_args,
    tags=["clickstream", "csv", "minio", "raw"],
) as dag:

    t0 = PythonOperator(task_id="ensure_ingested_table", python_callable=ensure_ingested_table)

    t1 = PythonOperator(
        task_id="list_minio",
        python_callable=list_minio_csv_objects,
        do_xcom_push=True,
    )

    t2 = PythonOperator(
        task_id="ingest_new_csvs",
        python_callable=ingest_new_csvs,
    )

    t3 = PythonOperator(task_id="check_recent_csv_raw", python_callable=check_recent_csv_raw)

    t0 >> t1 >> t2 >> t3
