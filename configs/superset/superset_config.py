# Superset configuration
import os

# Database connection
SQLALCHEMY_DATABASE_URI = os.environ.get(
    "SUPERSET_DATABASE_URI",
    "sqlite:////app/superset_home/superset.db"
)

SECRET_KEY = os.environ.get("SUPERSET_SECRET_KEY", "CHANGE_ME_SUPERSET_SECRET")

# Feature flags
FEATURE_FLAGS = {
    "ENABLE_TEMPLATE_PROCESSING": True,
}

# Allow embedded dashboards
TALISMAN_ENABLED = False
WTF_CSRF_ENABLED = False

# ClickHouse connection
CLICKHOUSE_HOST = "clickhouse-1"
CLICKHOUSE_PORT = 8123
CLICKHOUSE_USER = "clickstream"
CLICKHOUSE_PASSWORD = "clickstream123"
