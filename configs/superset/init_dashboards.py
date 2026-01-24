#!/usr/bin/env python3
"""
Скрипт автоматического создания дашбордов Superset для Clickstream Analytics.
Запускается после старта Superset для настройки:
- Подключения к ClickHouse
- Датасетов аналитических витрин
- Графиков и дашборда

ВАЖНО: Скрипт создаёт только подключение и датасеты (без метрик).
Метрики создаются через sync_columns после первого наполнения данными.
"""

import os
import sys
import time
import requests
import json
from typing import Optional, List, Dict, Any

SUPERSET_URL = os.environ.get("SUPERSET_URL", "http://localhost:8088")
SUPERSET_USER = os.environ.get("SUPERSET_USER", "admin")
SUPERSET_PASS = os.environ.get("SUPERSET_PASS", "admin")

CLICKHOUSE_HOST = os.environ.get("CLICKHOUSE_HOST", "clickhouse-1")
CLICKHOUSE_PORT = os.environ.get("CLICKHOUSE_PORT", "8123")
CLICKHOUSE_USER = os.environ.get("CLICKHOUSE_USER", "clickstream")
CLICKHOUSE_PASS = os.environ.get("CLICKHOUSE_PASS", "clickstream123")

# Флаг: ждать ли появления данных в таблицах (по умолчанию - нет)
WAIT_FOR_DATA = os.environ.get("SUPERSET_WAIT_FOR_DATA", "0") == "1"

# Предопределённые метрики для витрин
DATASET_METRICS = {
    "kpi_daily": [
        {"metric_name": "events", "expression": "SUM(events)", "metric_type": "count", "verbose_name": "Events"},
        {"metric_name": "users", "expression": "SUM(users)", "metric_type": "count", "verbose_name": "Users"},
        {"metric_name": "sessions", "expression": "SUM(sessions)", "metric_type": "count", "verbose_name": "Sessions"},
        {"metric_name": "views", "expression": "SUM(views)", "metric_type": "count", "verbose_name": "Views"},
        {"metric_name": "clicks", "expression": "SUM(clicks)", "metric_type": "count", "verbose_name": "Clicks"},
        {"metric_name": "purchases", "expression": "SUM(purchases)", "metric_type": "count", "verbose_name": "Purchases"},
        {"metric_name": "ctr", "expression": "AVG(ctr)", "metric_type": "metric", "verbose_name": "CTR"},
        {"metric_name": "conversion", "expression": "AVG(conversion)", "metric_type": "metric", "verbose_name": "Conversion"},
    ],
    "pages_daily": [
        {"metric_name": "views", "expression": "SUM(views)", "metric_type": "count", "verbose_name": "Views"},
        {"metric_name": "clicks", "expression": "SUM(clicks)", "metric_type": "count", "verbose_name": "Clicks"},
        {"metric_name": "users", "expression": "SUM(users)", "metric_type": "count", "verbose_name": "Users"},
    ],
    "session_quality_daily": [
        {"metric_name": "sessions", "expression": "SUM(sessions)", "metric_type": "count", "verbose_name": "Sessions"},
        {"metric_name": "avg_duration_sec", "expression": "AVG(avg_duration_sec)", "metric_type": "metric", "verbose_name": "Avg Duration (sec)"},
        {"metric_name": "bounce_rate", "expression": "AVG(bounce_rate)", "metric_type": "metric", "verbose_name": "Bounce Rate"},
        {"metric_name": "avg_events_per_session", "expression": "AVG(avg_events_per_session)", "metric_type": "metric", "verbose_name": "Avg Events/Session"},
    ],
    "referrer_daily": [
        {"metric_name": "events", "expression": "SUM(events)", "metric_type": "count", "verbose_name": "Events"},
        {"metric_name": "users", "expression": "SUM(users)", "metric_type": "count", "verbose_name": "Users"},
        {"metric_name": "sessions", "expression": "SUM(sessions)", "metric_type": "count", "verbose_name": "Sessions"},
    ],
    "user_cohorts_daily": [
        {"metric_name": "new_users", "expression": "SUM(new_users)", "metric_type": "count", "verbose_name": "New Users"},
        {"metric_name": "returning_users", "expression": "SUM(returning_users)", "metric_type": "count", "verbose_name": "Returning Users"},
        {"metric_name": "heavy_users", "expression": "SUM(heavy_users)", "metric_type": "count", "verbose_name": "Heavy Users"},
        {"metric_name": "light_users", "expression": "SUM(light_users)", "metric_type": "count", "verbose_name": "Light Users"},
    ],
}


class SupersetAPI:
    def __init__(self, base_url: str, username: str, password: str):
        self.base_url = base_url.rstrip("/")
        self.session = requests.Session()
        self.access_token: Optional[str] = None
        self.csrf_token: Optional[str] = None
        self._login(username, password)

    def _login(self, username: str, password: str):
        """Авторизация и получение токенов"""
        resp = self.session.post(
            f"{self.base_url}/api/v1/security/login",
            json={"username": username, "password": password, "provider": "db", "refresh": True}
        )
        resp.raise_for_status()
        self.access_token = resp.json()["access_token"]
        self.session.headers["Authorization"] = f"Bearer {self.access_token}"

        resp = self.session.get(f"{self.base_url}/api/v1/security/csrf_token/")
        resp.raise_for_status()
        self.csrf_token = resp.json()["result"]
        self.session.headers["X-CSRFToken"] = self.csrf_token
        self.session.headers["Referer"] = self.base_url

    def get(self, endpoint: str) -> dict:
        resp = self.session.get(f"{self.base_url}/api/v1/{endpoint}")
        resp.raise_for_status()
        return resp.json()

    def post(self, endpoint: str, data: dict) -> dict:
        resp = self.session.post(f"{self.base_url}/api/v1/{endpoint}", json=data)
        if not resp.ok:
            print(f"POST {endpoint} failed: {resp.status_code} {resp.text[:500]}")
        resp.raise_for_status()
        return resp.json()

    def put(self, endpoint: str, data: dict) -> dict:
        resp = self.session.put(f"{self.base_url}/api/v1/{endpoint}", json=data)
        if not resp.ok:
            print(f"PUT {endpoint} failed: {resp.status_code} {resp.text[:500]}")
        resp.raise_for_status()
        return resp.json()

    def delete(self, endpoint: str) -> bool:
        resp = self.session.delete(f"{self.base_url}/api/v1/{endpoint}")
        return resp.ok


def wait_for_superset(url: str, max_retries: int = 60, delay: int = 5):
    """Ожидание готовности Superset"""
    print(f"Ожидание Superset на {url}...")
    for i in range(max_retries):
        try:
            resp = requests.get(f"{url}/health", timeout=5)
            if resp.ok:
                print("Superset готов!")
                return True
        except requests.exceptions.RequestException:
            pass
        print(f"  попытка {i+1}/{max_retries}...")
        time.sleep(delay)
    raise Exception("Superset не запустился")


def check_dashboard_exists(api: SupersetAPI) -> bool:
    """Проверяем, существует ли дашборд с привязанными чартами"""
    try:
        # Проверяем дашборд по slug
        resp = api.session.get(f"{api.base_url}/api/v1/dashboard/clickstream-analytics/charts")
        if resp.ok:
            charts = resp.json().get("result", [])
            if len(charts) >= 8:
                print(f"Дашборд уже существует и содержит {len(charts)} чартов - пропускаем инициализацию")
                return True
    except Exception:
        pass
    return False


def cleanup_existing(api: SupersetAPI):
    """Удаление существующих объектов для чистой переинициализации"""
    print("Очистка существующих объектов...")

    # Удаляем дашборды
    dashboards = api.get("dashboard/")
    for dash in dashboards.get("result", []):
        if dash.get("dashboard_title") == "Clickstream Analytics":
            api.delete(f"dashboard/{dash['id']}")
            print(f"  удалён дашборд (id={dash['id']})")

    # Удаляем чарты
    charts = api.get("chart/")
    chart_names = [
        "DAU (Активные пользователи)", "CTR (Клики/Просмотры)", "Конверсия",
        "Длительность сессии (сек)", "Топ-10 страниц", "Источники трафика",
        "Новые vs Вернувшиеся", "Bounce Rate"
    ]
    for ch in charts.get("result", []):
        if ch.get("slice_name") in chart_names:
            api.delete(f"chart/{ch['id']}")
            print(f"  удалён чарт '{ch['slice_name']}' (id={ch['id']})")


def create_database_connection(api: SupersetAPI) -> int:
    """Создание подключения к ClickHouse"""
    print("Создание подключения к ClickHouse...")

    existing = api.get("database/")
    for db in existing.get("result", []):
        if db.get("database_name") == "ClickHouse Analytics":
            print(f"  подключение уже существует (id={db['id']})")
            return db["id"]

    sqlalchemy_uri = (
        f"clickhousedb+connect://{CLICKHOUSE_USER}:{CLICKHOUSE_PASS}"
        f"@{CLICKHOUSE_HOST}:{CLICKHOUSE_PORT}/analytics"
    )

    result = api.post("database/", {
        "database_name": "ClickHouse Analytics",
        "sqlalchemy_uri": sqlalchemy_uri,
        "expose_in_sqllab": True,
        "allow_run_async": True,
        "allow_ctas": False,
        "allow_cvas": False
    })

    db_id = result["id"]
    print(f"  подключение создано (id={db_id})")
    return db_id


def create_or_update_dataset(api: SupersetAPI, db_id: int, table_name: str, schema: str = "analytics") -> int:
    """
    Создание датасета и метрик.

    Важно: сначала создаём датасет, потом синхронизируем колонки,
    и только после этого добавляем метрики. Это гарантирует что
    Superset знает о колонках таблицы.
    """
    print(f"Создание/обновление датасета {schema}.{table_name}...")

    existing = api.get("dataset/")
    ds_id = None
    for ds in existing.get("result", []):
        if ds.get("table_name") == table_name and ds.get("schema") == schema:
            ds_id = ds["id"]
            print(f"  датасет уже существует (id={ds_id})")
            break

    if ds_id is None:
        result = api.post("dataset/", {
            "database": db_id,
            "schema": schema,
            "table_name": table_name
        })
        ds_id = result["id"]
        print(f"  датасет создан (id={ds_id})")

    # Синхронизируем колонки из ClickHouse
    try:
        api.put(f"dataset/{ds_id}/refresh", {})
        print(f"  колонки синхронизированы")
    except Exception as e:
        print(f"  ошибка синхронизации колонок: {e}")

    # Добавляем метрики
    metrics = DATASET_METRICS.get(table_name, [])
    if metrics:
        try:
            api.put(f"dataset/{ds_id}", {"metrics": metrics})
            print(f"  добавлены метрики: {[m['metric_name'] for m in metrics]}")
        except Exception as e:
            print(f"  ошибка добавления метрик (таблица может быть пустой): {e}")
            # Не падаем - метрики можно добавить позже

    return ds_id


def make_adhoc_metric(column: str, aggregate: str = "SUM", label: str = None) -> dict:
    """
    Создаёт adhoc metric для графика.
    Adhoc metrics не требуют предварительного сохранения метрик в датасете.
    """
    return {
        "expressionType": "SIMPLE",
        "column": {"column_name": column},
        "aggregate": aggregate,
        "label": label or f"{aggregate}({column})"
    }


def make_sql_metric(expression: str, label: str) -> dict:
    """Создаёт SQL adhoc metric."""
    return {
        "expressionType": "SQL",
        "sqlExpression": expression,
        "label": label
    }


def create_chart(api: SupersetAPI, name: str, viz_type: str, datasource_id: int,
                 params: Dict[str, Any], description: str = "") -> int:
    """
    Создание графика с adhoc metrics.

    Важно: используем adhoc_metrics вместо ссылок на saved metrics,
    чтобы графики работали даже если метрики ещё не созданы.
    """
    print(f"Создание графика '{name}'...")

    full_params = {
        "datasource": f"{datasource_id}__table",
        "viz_type": viz_type,
        "orderby": [],
        "order_desc": False,
        "order_by_cols": [],
        **params
    }

    result = api.post("chart/", {
        "slice_name": name,
        "viz_type": viz_type,
        "datasource_id": datasource_id,
        "datasource_type": "table",
        "description": description,
        "params": json.dumps(full_params)
    })

    chart_id = result["id"]
    print(f"  график создан (id={chart_id})")
    return chart_id


def create_dashboard_with_charts(api: SupersetAPI, title: str, chart_ids: list) -> int:
    """Создание дашборда с графиками через добавление чартов к дашборду"""
    print(f"Создание дашборда '{title}'...")

    # Создаём дашборд
    result = api.post("dashboard/", {
        "dashboard_title": title,
        "slug": "clickstream-analytics",
        "published": True,
    })
    dash_id = result["id"]
    print(f"  дашборд создан (id={dash_id})")

    # Привязываем каждый чарт к дашборду через обновление чарта
    for chart_id in chart_ids:
        try:
            # Получаем текущие дашборды чарта
            chart_data = api.get(f"chart/{chart_id}")
            current_dashboards = [d["id"] for d in chart_data.get("result", {}).get("dashboards", [])]
            if dash_id not in current_dashboards:
                current_dashboards.append(dash_id)

            api.put(f"chart/{chart_id}", {
                "dashboards": current_dashboards
            })
            print(f"  чарт {chart_id} привязан к дашборду")
        except Exception as e:
            print(f"  ошибка привязки чарта {chart_id}: {e}")

    # Создаем layout для графиков (сетка 12 колонок)
    position_json = {
        "DASHBOARD_VERSION_KEY": "v2",
        "ROOT_ID": {"type": "ROOT", "id": "ROOT_ID", "children": ["GRID_ID"]},
        "GRID_ID": {"type": "GRID", "id": "GRID_ID", "children": [], "parents": ["ROOT_ID"]},
        "HEADER_ID": {"type": "HEADER", "id": "HEADER_ID", "meta": {"text": title}}
    }

    # Размещаем графики в строки по 2
    for i, chart_id in enumerate(chart_ids):
        row_id = f"ROW-{i//2}"
        chart_key = f"CHART-{chart_id}"

        if i % 2 == 0:
            position_json[row_id] = {
                "type": "ROW",
                "id": row_id,
                "children": [],
                "parents": ["GRID_ID"],
                "meta": {"background": "BACKGROUND_TRANSPARENT"}
            }
            position_json["GRID_ID"]["children"].append(row_id)

        position_json[chart_key] = {
            "type": "CHART",
            "id": chart_key,
            "children": [],
            "parents": [row_id],
            "meta": {
                "width": 6,
                "height": 50,
                "chartId": chart_id,
                "sliceName": f"Chart {chart_id}"
            }
        }
        position_json[row_id]["children"].append(chart_key)

    # Обновляем дашборд с position_json
    api.put(f"dashboard/{dash_id}", {
        "position_json": json.dumps(position_json),
        "json_metadata": json.dumps({
            "timed_refresh_immune_slices": [],
            "expanded_slices": {},
            "refresh_frequency": 0,
            "default_filters": "{}",
            "color_scheme": "supersetColors",
            "label_colors": {},
            "chart_configuration": {}
        })
    })
    print(f"  layout обновлён")

    return dash_id


def main():
    print("=" * 60)
    print("Инициализация Superset дашбордов для Clickstream Analytics")
    print("=" * 60)

    wait_for_superset(SUPERSET_URL)
    time.sleep(10)

    print("\nПодключение к Superset API...")
    api = SupersetAPI(SUPERSET_URL, SUPERSET_USER, SUPERSET_PASS)
    print("  успешно!")

    # Проверяем, нужна ли инициализация
    if check_dashboard_exists(api):
        print("\n" + "=" * 60)
        print("Дашборд уже настроен, инициализация не требуется")
        print("=" * 60)
        return

    # Удаляем старые объекты для чистой переинициализации
    cleanup_existing(api)

    db_id = create_database_connection(api)

    datasets = {}
    tables = [
        "kpi_daily",
        "pages_daily",
        "session_quality_daily",
        "referrer_daily",
        "user_cohorts_daily"
    ]

    for table in tables:
        datasets[table] = create_or_update_dataset(api, db_id, table)

    chart_ids = []

    # 1. DAU - линейный график
    chart_ids.append(create_chart(
        api,
        name="DAU (Активные пользователи)",
        viz_type="echarts_timeseries_line",
        datasource_id=datasets["kpi_daily"],
        params={
            "metrics": [make_adhoc_metric("users", "SUM", "Users")],
            "x_axis": "day",
            "time_range": "Last 30 days",
            "row_limit": 1000
        }
    ))

    # 2. CTR - линейный график
    chart_ids.append(create_chart(
        api,
        name="CTR (Клики/Просмотры)",
        viz_type="echarts_timeseries_line",
        datasource_id=datasets["kpi_daily"],
        params={
            "metrics": [make_adhoc_metric("ctr", "AVG", "CTR")],
            "x_axis": "day",
            "time_range": "Last 30 days"
        }
    ))

    # 3. Conversion - линейный график
    chart_ids.append(create_chart(
        api,
        name="Конверсия",
        viz_type="echarts_timeseries_line",
        datasource_id=datasets["kpi_daily"],
        params={
            "metrics": [make_adhoc_metric("conversion", "AVG", "Conversion")],
            "x_axis": "day",
            "time_range": "Last 30 days"
        }
    ))

    # 4. Session Duration - bar chart
    chart_ids.append(create_chart(
        api,
        name="Длительность сессии (сек)",
        viz_type="echarts_timeseries_bar",
        datasource_id=datasets["session_quality_daily"],
        params={
            "metrics": [make_adhoc_metric("avg_duration_sec", "AVG", "Avg Duration (sec)")],
            "x_axis": "day",
            "time_range": "Last 30 days"
        }
    ))

    # 5. Top Pages - таблица
    chart_ids.append(create_chart(
        api,
        name="Топ-10 страниц",
        viz_type="table",
        datasource_id=datasets["pages_daily"],
        params={
            "metrics": [
                make_adhoc_metric("views", "SUM", "Views"),
                make_adhoc_metric("clicks", "SUM", "Clicks")
            ],
            "groupby": ["page_url"],
            "row_limit": 10,
            "time_range": "Last 30 days",
            "order_desc": True
        }
    ))

    # 6. Traffic Sources - pie chart
    chart_ids.append(create_chart(
        api,
        name="Источники трафика",
        viz_type="pie",
        datasource_id=datasets["referrer_daily"],
        params={
            "metric": make_adhoc_metric("events", "SUM", "Events"),
            "groupby": ["referrer"],
            "row_limit": 10,
            "time_range": "Last 30 days"
        }
    ))

    # 7. New vs Returning - area chart
    chart_ids.append(create_chart(
        api,
        name="Новые vs Вернувшиеся",
        viz_type="echarts_area",
        datasource_id=datasets["user_cohorts_daily"],
        params={
            "metrics": [
                make_adhoc_metric("new_users", "SUM", "New Users"),
                make_adhoc_metric("returning_users", "SUM", "Returning Users")
            ],
            "x_axis": "day",
            "time_range": "Last 30 days"
        }
    ))

    # 8. Bounce Rate - big number
    chart_ids.append(create_chart(
        api,
        name="Bounce Rate",
        viz_type="big_number_total",
        datasource_id=datasets["session_quality_daily"],
        params={
            "metric": make_adhoc_metric("bounce_rate", "AVG", "Bounce Rate"),
            "time_range": "Last 7 days"
        }
    ))

    create_dashboard_with_charts(api, "Clickstream Analytics", chart_ids)

    print("\n" + "=" * 60)
    print("Инициализация завершена!")
    print(f"Дашборд: {SUPERSET_URL}/superset/dashboard/clickstream-analytics/")
    print("=" * 60)


if __name__ == "__main__":
    try:
        main()
    except Exception as e:
        print(f"\nОШИБКА: {e}")
        import traceback
        traceback.print_exc()
        sys.exit(1)
