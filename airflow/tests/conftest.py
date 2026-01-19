"""Pytest fixtures for Airflow DAG tests."""
import os
import sys
from unittest.mock import MagicMock, patch

import pytest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "dags"))


@pytest.fixture
def mock_clickhouse_client():
    """Mock ClickHouse client for testing."""
    mock_client = MagicMock()
    mock_client.execute.return_value = [(1,)]
    return mock_client


@pytest.fixture
def mock_boto3_client():
    """Mock boto3 S3 client for MinIO testing."""
    mock_s3 = MagicMock()
    mock_s3.list_objects_v2.return_value = {
        "Contents": [
            {"Key": "events_contract_v1_001.csv", "ETag": '"abc123"'},
            {"Key": "events_contract_v1_002.csv", "ETag": '"def456"'},
        ],
        "IsTruncated": False,
    }
    return mock_s3


@pytest.fixture
def mock_env_vars(monkeypatch):
    """Set up environment variables for testing."""
    monkeypatch.setenv("CLICKHOUSE_HOST", "localhost")
    monkeypatch.setenv("CLICKHOUSE_PORT", "9000")
    monkeypatch.setenv("CLICKHOUSE_USER", "test_user")
    monkeypatch.setenv("CLICKHOUSE_PASSWORD", "test_pass")
    monkeypatch.setenv("CLICKHOUSE_DB", "clickstream")
    monkeypatch.setenv("MINIO_ENDPOINT", "http://localhost:9000")
    monkeypatch.setenv("MINIO_ACCESS_KEY", "minioadmin")
    monkeypatch.setenv("MINIO_SECRET_KEY", "minioadmin123")
    monkeypatch.setenv("MINIO_BUCKET", "click-analysis")


@pytest.fixture
def mock_xcom_context():
    """Mock Airflow XCom context."""
    ti = MagicMock()
    ti.xcom_pull.return_value = {
        "bucket": "click-analysis",
        "objects": [("events_contract_v1_001.csv", "abc123")],
        "prefix": "events_contract_v1_",
    }
    return {"ti": ti, "run_id": "test_run_123"}


@pytest.fixture
def sample_csv_objects():
    """Sample CSV object list."""
    return [
        ("events_contract_v1_20240101_001.csv", "etag1"),
        ("events_contract_v1_20240101_002.csv", "etag2"),
        ("events_contract_v1_20240102_001.csv", "etag3"),
    ]
