"""Tests for DAG 02: Dedup, Prepare, and Aggregate."""
import os
import sys
from datetime import datetime
from unittest.mock import MagicMock, patch

import pytest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "dags"))


class TestEventTypes:
    """Tests for EVENT_TYPES constant."""

    def test_event_types_defined(self):
        """Test that EVENT_TYPES contains expected values."""
        from dag_02_dedup_prepare_aggregate import EVENT_TYPES

        assert "click" in EVENT_TYPES
        assert "view" in EVENT_TYPES
        assert "purchase" in EVENT_TYPES
        assert "signup" in EVENT_TYPES
        assert "login" in EVENT_TYPES
        assert "logout" in EVENT_TYPES
        assert len(EVENT_TYPES) == 6


class TestDagDefinition:
    """Tests for DAG definition."""

    def test_dag_is_valid(self):
        """Test that DAG is valid."""
        from dag_02_dedup_prepare_aggregate import dag

        assert dag is not None
        assert dag.dag_id == "clickstream_02_dedup_prepare_aggregate"

    def test_dag_has_correct_schedule(self):
        """Test DAG schedule."""
        from dag_02_dedup_prepare_aggregate import dag

        assert dag.schedule == "*/2 * * * *"

    def test_dag_has_required_tasks(self):
        """Test that DAG has all required tasks."""
        from dag_02_dedup_prepare_aggregate import dag

        task_ids = [task.task_id for task in dag.tasks]

        assert "ensure_tables" in task_ids
        assert "dedup_prepare_aggregate" in task_ids

    def test_dag_task_dependencies(self):
        """Test task dependencies."""
        from dag_02_dedup_prepare_aggregate import dag

        tasks = {task.task_id: task for task in dag.tasks}

        # t0 >> t1
        assert "dedup_prepare_aggregate" in [t.task_id for t in tasks["ensure_tables"].downstream_list]

    def test_dag_has_correct_tags(self):
        """Test DAG tags."""
        from dag_02_dedup_prepare_aggregate import dag

        assert "clickstream" in dag.tags
        assert "dedup" in dag.tags
        assert "aggregate" in dag.tags

    def test_dag_default_args(self):
        """Test default args."""
        from dag_02_dedup_prepare_aggregate import dag

        assert dag.default_args["owner"] == "clickstream"
        assert dag.default_args["retries"] == 2


class TestChExecute:
    """Tests for ch_execute function."""

    @patch("dag_02_dedup_prepare_aggregate._get_client")
    def test_ch_execute_without_settings(self, mock_get_client):
        """Test SQL execution without settings."""
        from dag_02_dedup_prepare_aggregate import ch_execute

        mock_client = MagicMock()
        mock_get_client.return_value = mock_client

        ch_execute("SELECT 1")

        mock_client.execute.assert_called_once_with("SELECT 1")

    @patch("dag_02_dedup_prepare_aggregate._get_client")
    def test_ch_execute_with_settings(self, mock_get_client):
        """Test SQL execution with settings."""
        from dag_02_dedup_prepare_aggregate import ch_execute

        mock_client = MagicMock()
        mock_get_client.return_value = mock_client

        ch_execute("SELECT 1", settings={"max_execution_time": 300})

        mock_client.execute.assert_called_once_with(
            "SELECT 1", settings={"max_execution_time": 300}
        )


class TestChQuery:
    """Tests for ch_query function."""

    @patch("dag_02_dedup_prepare_aggregate._get_client")
    def test_ch_query_returns_result(self, mock_get_client):
        """Test query returns result."""
        from dag_02_dedup_prepare_aggregate import ch_query

        mock_client = MagicMock()
        mock_client.execute.return_value = [(1, "test")]
        mock_get_client.return_value = mock_client

        result = ch_query("SELECT 1, 'test'")

        assert result == [(1, "test")]


class TestEnsureTables:
    """Tests for ensure_tables function."""

    @patch("dag_02_dedup_prepare_aggregate.ch_execute")
    def test_ensure_tables_creates_all_tables(self, mock_ch_execute):
        """Test that all required tables are created."""
        from dag_02_dedup_prepare_aggregate import ensure_tables

        ensure_tables()

        assert mock_ch_execute.call_count == 4

        calls_sql = [str(c) for c in mock_ch_execute.call_args_list]
        sql_combined = "".join(calls_sql)

        assert "etl_state" in sql_combined
        assert "prepared_events" in sql_combined
        assert "session_metrics_5m" in sql_combined

    @patch("dag_02_dedup_prepare_aggregate.ch_execute")
    def test_ensure_tables_creates_etl_state(self, mock_ch_execute):
        """Test etl_state table creation."""
        from dag_02_dedup_prepare_aggregate import ensure_tables

        ensure_tables()

        first_call = mock_ch_execute.call_args_list[0][0][0]
        assert "CREATE TABLE IF NOT EXISTS clickstream.etl_state" in first_call

    @patch("dag_02_dedup_prepare_aggregate.ch_execute")
    def test_ensure_tables_creates_prepared_events(self, mock_ch_execute):
        """Test prepared_events table creation."""
        from dag_02_dedup_prepare_aggregate import ensure_tables

        ensure_tables()

        third_call = mock_ch_execute.call_args_list[2][0][0]
        assert "CREATE TABLE IF NOT EXISTS clickstream.prepared_events" in third_call
        assert "ReplacingMergeTree" in third_call

    @patch("dag_02_dedup_prepare_aggregate.ch_execute")
    def test_ensure_tables_creates_session_metrics(self, mock_ch_execute):
        """Test session_metrics_5m table creation."""
        from dag_02_dedup_prepare_aggregate import ensure_tables

        ensure_tables()

        fourth_call = mock_ch_execute.call_args_list[3][0][0]
        assert "CREATE TABLE IF NOT EXISTS clickstream.session_metrics_5m" in fourth_call
        assert "SummingMergeTree" in fourth_call


class TestDedupPrepareAggregate:
    """Tests for dedup_prepare_aggregate function."""

    @patch("dag_02_dedup_prepare_aggregate.ch_execute")
    @patch("dag_02_dedup_prepare_aggregate.ch_query")
    def test_dedup_processes_incremental_window(self, mock_ch_query, mock_ch_execute):
        """Test incremental window processing."""
        from dag_02_dedup_prepare_aggregate import dedup_prepare_aggregate

        last_ts = datetime(2024, 1, 1, 0, 0, 0)
        upper_ts = datetime(2024, 1, 1, 1, 0, 0)

        mock_ch_query.side_effect = [
            [(last_ts,)],  # last_ts query
            [(upper_ts,)],  # upper_ts query
        ]

        dedup_prepare_aggregate()

        # Should have 3 executes: prepared_events insert, aggregates insert, watermark update
        assert mock_ch_execute.call_count == 3

    @patch("dag_02_dedup_prepare_aggregate.ch_execute")
    @patch("dag_02_dedup_prepare_aggregate.ch_query")
    def test_dedup_inserts_into_prepared_events(self, mock_ch_query, mock_ch_execute):
        """Test insert into prepared_events."""
        from dag_02_dedup_prepare_aggregate import dedup_prepare_aggregate

        mock_ch_query.side_effect = [
            [(datetime(2024, 1, 1),)],
            [(datetime(2024, 1, 1, 1),)],
        ]

        dedup_prepare_aggregate()

        first_call = mock_ch_execute.call_args_list[0]
        sql = first_call[0][0]
        assert "INSERT INTO clickstream.prepared_events" in sql
        assert "dedup_key" in sql

    @patch("dag_02_dedup_prepare_aggregate.ch_execute")
    @patch("dag_02_dedup_prepare_aggregate.ch_query")
    def test_dedup_inserts_into_session_metrics(self, mock_ch_query, mock_ch_execute):
        """Test insert into session_metrics_5m."""
        from dag_02_dedup_prepare_aggregate import dedup_prepare_aggregate

        mock_ch_query.side_effect = [
            [(datetime(2024, 1, 1),)],
            [(datetime(2024, 1, 1, 1),)],
        ]

        dedup_prepare_aggregate()

        second_call = mock_ch_execute.call_args_list[1]
        sql = second_call[0][0]
        assert "INSERT INTO clickstream.session_metrics_5m" in sql

    @patch("dag_02_dedup_prepare_aggregate.ch_execute")
    @patch("dag_02_dedup_prepare_aggregate.ch_query")
    def test_dedup_updates_watermark(self, mock_ch_query, mock_ch_execute):
        """Test watermark update."""
        from dag_02_dedup_prepare_aggregate import dedup_prepare_aggregate

        mock_ch_query.side_effect = [
            [(datetime(2024, 1, 1),)],
            [(datetime(2024, 1, 1, 1),)],
        ]

        dedup_prepare_aggregate()

        third_call = mock_ch_execute.call_args_list[2]
        sql = third_call[0][0]
        assert "ALTER TABLE clickstream.etl_state" in sql
        assert "UPDATE last_ts" in sql

    @patch("dag_02_dedup_prepare_aggregate.ch_execute")
    @patch("dag_02_dedup_prepare_aggregate.ch_query")
    def test_dedup_key_formats(self, mock_ch_query, mock_ch_execute):
        """Test dedup_key format for different sources."""
        from dag_02_dedup_prepare_aggregate import dedup_prepare_aggregate

        mock_ch_query.side_effect = [
            [(datetime(2024, 1, 1),)],
            [(datetime(2024, 1, 1, 1),)],
        ]

        dedup_prepare_aggregate()

        sql = mock_ch_execute.call_args_list[0][0][0]
        # Kafka format
        assert "source = 'kafka'" in sql
        assert "source_topic" in sql
        # CSV format
        assert "source = 'csv'" in sql
        assert "source_object" in sql


class TestPreparedEventsSchema:
    """Tests for prepared_events table schema."""

    @patch("dag_02_dedup_prepare_aggregate.ch_execute")
    def test_prepared_events_has_all_columns(self, mock_ch_execute):
        """Test prepared_events has all required columns."""
        from dag_02_dedup_prepare_aggregate import ensure_tables

        ensure_tables()

        schema_call = mock_ch_execute.call_args_list[2][0][0]

        required_columns = [
            "dedup_key", "prepared_time", "event_id", "event_time",
            "user_id", "session_id", "device_id", "event_type",
            "page_url", "page_path", "page_query", "referrer",
            "user_agent", "ip", "props_json", "source"
        ]

        for col in required_columns:
            assert col in schema_call, f"Missing column: {col}"


class TestSessionMetricsSchema:
    """Tests for session_metrics_5m table schema."""

    @patch("dag_02_dedup_prepare_aggregate.ch_execute")
    def test_session_metrics_has_all_columns(self, mock_ch_execute):
        """Test session_metrics_5m has all required columns."""
        from dag_02_dedup_prepare_aggregate import ensure_tables

        ensure_tables()

        schema_call = mock_ch_execute.call_args_list[3][0][0]

        required_columns = [
            "window_start", "session_id", "user_id",
            "events_count", "clicks", "views", "purchases",
            "logins", "signups", "logouts", "uniq_pages"
        ]

        for col in required_columns:
            assert col in schema_call, f"Missing column: {col}"
