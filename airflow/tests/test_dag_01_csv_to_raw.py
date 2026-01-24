"""Tests for DAG 01: CSV to Raw Events."""
import os
import sys
from datetime import datetime
from unittest.mock import MagicMock, patch

import pytest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "dags"))


class TestDagDefinition:
    """Tests for DAG definition."""

    def test_dag_is_valid(self):
        """Test that DAG is valid and can be loaded."""
        from dag_01_csv_to_raw import dag

        assert dag is not None
        assert dag.dag_id == "clickstream_01_csv_to_raw"

    def test_dag_has_correct_schedule(self):
        """Test DAG schedule."""
        from dag_01_csv_to_raw import dag

        assert dag.schedule == "*/5 * * * *"

    def test_dag_has_required_tasks(self):
        """Test that DAG has all required tasks."""
        from dag_01_csv_to_raw import dag

        task_ids = [task.task_id for task in dag.tasks]

        assert "ensure_ingested_table" in task_ids
        assert "list_minio" in task_ids
        assert "ingest_new_csvs" in task_ids
        assert "check_recent_csv_raw" in task_ids

    def test_dag_task_dependencies(self):
        """Test task dependencies are correct."""
        from dag_01_csv_to_raw import dag

        tasks = {task.task_id: task for task in dag.tasks}

        assert "list_minio" in [t.task_id for t in tasks["ensure_ingested_table"].downstream_list]
        assert "ingest_new_csvs" in [t.task_id for t in tasks["list_minio"].downstream_list]
        assert "check_recent_csv_raw" in [t.task_id for t in tasks["ingest_new_csvs"].downstream_list]

    def test_dag_has_correct_tags(self):
        """Test DAG tags."""
        from dag_01_csv_to_raw import dag

        assert "clickstream" in dag.tags
        assert "csv" in dag.tags
        assert "minio" in dag.tags

    def test_dag_default_args(self):
        """Test default args."""
        from dag_01_csv_to_raw import dag

        assert dag.default_args["owner"] == "clickstream"
        assert dag.default_args["retries"] == 2


class TestChExecute:
    """Tests for ch_execute function."""

    @patch("dag_01_csv_to_raw._get_client")
    def test_ch_execute_simple_sql(self, mock_get_client):
        """Test simple SQL execution."""
        from dag_01_csv_to_raw import ch_execute

        mock_client = MagicMock()
        mock_get_client.return_value = mock_client

        ch_execute("SELECT 1")

        mock_client.execute.assert_called_once_with("SELECT 1", settings=None)

    @patch("dag_01_csv_to_raw._get_client")
    def test_ch_execute_with_timeout(self, mock_get_client):
        """Test SQL execution with timeout."""
        from dag_01_csv_to_raw import ch_execute

        mock_client = MagicMock()
        mock_get_client.return_value = mock_client

        ch_execute("SELECT 1", timeout=60)

        mock_client.execute.assert_called_once_with(
            "SELECT 1", settings={"max_execution_time": 60}
        )


class TestChQuery:
    """Tests for ch_query function."""

    @patch("dag_01_csv_to_raw._get_client")
    def test_ch_query_returns_result(self, mock_get_client):
        """Test query returns result."""
        from dag_01_csv_to_raw import ch_query

        mock_client = MagicMock()
        mock_client.execute.return_value = [(1, "test")]
        mock_get_client.return_value = mock_client

        result = ch_query("SELECT 1, 'test'")

        assert result == [(1, "test")]


class TestEnsureIngestedTable:
    """Tests for ensure_ingested_table function."""

    @patch("dag_01_csv_to_raw.ch_execute")
    def test_ensure_ingested_table_creates_table(self, mock_ch_execute):
        """Test that table is created."""
        from dag_01_csv_to_raw import ensure_ingested_table

        ensure_ingested_table()

        mock_ch_execute.assert_called_once()
        call_args = mock_ch_execute.call_args[0][0]
        assert "CREATE TABLE IF NOT EXISTS clickstream.ingested_objects" in call_args


class TestListMinioCsvObjects:
    """Tests for list_minio_csv_objects function."""

    @patch("boto3.client")
    def test_list_objects_returns_csv_files(self, mock_boto3_client, mock_env_vars):
        """Test listing CSV objects from MinIO."""
        from dag_01_csv_to_raw import list_minio_csv_objects

        mock_s3 = MagicMock()
        mock_s3.list_objects_v2.return_value = {
            "Contents": [
                {"Key": "events_contract_v1_001.csv", "ETag": '"abc123"'},
                {"Key": "events_contract_v1_002.csv", "ETag": '"def456"'},
                {"Key": "other_file.txt", "ETag": '"xyz"'},
            ],
            "IsTruncated": False,
        }
        mock_boto3_client.return_value = mock_s3

        result = list_minio_csv_objects()

        assert result["bucket"] == "click-analysis"
        assert len(result["objects"]) == 2
        assert ("events_contract_v1_001.csv", "abc123") in result["objects"]
        assert ("events_contract_v1_002.csv", "def456") in result["objects"]

    @patch("boto3.client")
    def test_list_objects_handles_pagination(self, mock_boto3_client, mock_env_vars):
        """Test pagination handling."""
        from dag_01_csv_to_raw import list_minio_csv_objects

        mock_s3 = MagicMock()
        mock_s3.list_objects_v2.side_effect = [
            {
                "Contents": [{"Key": "events_contract_v1_001.csv", "ETag": '"a"'}],
                "IsTruncated": True,
                "NextContinuationToken": "token123",
            },
            {
                "Contents": [{"Key": "events_contract_v1_002.csv", "ETag": '"b"'}],
                "IsTruncated": False,
            },
        ]
        mock_boto3_client.return_value = mock_s3

        result = list_minio_csv_objects()

        assert len(result["objects"]) == 2
        assert mock_s3.list_objects_v2.call_count == 2

    @patch("boto3.client")
    def test_list_objects_empty_bucket(self, mock_boto3_client, mock_env_vars):
        """Test empty bucket handling."""
        from dag_01_csv_to_raw import list_minio_csv_objects

        mock_s3 = MagicMock()
        mock_s3.list_objects_v2.return_value = {"IsTruncated": False}
        mock_boto3_client.return_value = mock_s3

        result = list_minio_csv_objects()

        assert result["objects"] == []


class TestIngestNewCsvs:
    """Tests for ingest_new_csvs function."""

    @patch("dag_01_csv_to_raw.ch_execute")
    @patch("dag_01_csv_to_raw.ch_query")
    def test_ingest_skips_already_ingested(self, mock_ch_query, mock_ch_execute, mock_env_vars):
        """Test that already ingested objects are skipped."""
        from dag_01_csv_to_raw import ingest_new_csvs

        mock_ch_query.return_value = [("events_contract_v1_001.csv",)]

        mock_ti = MagicMock()
        mock_ti.xcom_pull.return_value = {
            "bucket": "click-analysis",
            "objects": [("events_contract_v1_001.csv", "abc123")],
            "prefix": "events_contract_v1_",
        }
        context = {"ti": mock_ti, "run_id": "test_run"}

        ingest_new_csvs(**context)

        insert_calls = [c for c in mock_ch_execute.call_args_list
                       if "INSERT INTO clickstream.raw_events" in str(c)]
        assert len(insert_calls) == 0

    @patch("dag_01_csv_to_raw.ch_execute")
    @patch("dag_01_csv_to_raw.ch_query")
    def test_ingest_processes_new_objects(self, mock_ch_query, mock_ch_execute, mock_env_vars):
        """Test that new objects are processed."""
        from dag_01_csv_to_raw import ingest_new_csvs

        mock_ch_query.return_value = []

        mock_ti = MagicMock()
        mock_ti.xcom_pull.return_value = {
            "bucket": "click-analysis",
            "objects": [("events_contract_v1_001.csv", "abc123")],
            "prefix": "events_contract_v1_",
        }
        context = {"ti": mock_ti, "run_id": "test_run"}

        ingest_new_csvs(**context)

        assert mock_ch_execute.call_count >= 2

    def test_ingest_no_data_returns_early(self, mock_env_vars):
        """Test that function returns early when no data."""
        from dag_01_csv_to_raw import ingest_new_csvs

        mock_ti = MagicMock()
        mock_ti.xcom_pull.return_value = None
        context = {"ti": mock_ti, "run_id": "test_run"}

        ingest_new_csvs(**context)


class TestCheckRecentCsvRaw:
    """Tests for check_recent_csv_raw function."""

    @patch("dag_01_csv_to_raw.ch_query")
    def test_check_recent_returns_stats(self, mock_ch_query):
        """Test that check returns statistics."""
        from dag_01_csv_to_raw import check_recent_csv_raw

        mock_ch_query.return_value = [(100, datetime.now(), datetime.now())]

        check_recent_csv_raw()

        mock_ch_query.assert_called_once()
        assert "raw_events" in mock_ch_query.call_args[0][0]
