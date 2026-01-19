"""Tests for DAG 03: Fill Analytics Marts."""
import os
import sys
from datetime import datetime, date
from unittest.mock import MagicMock, patch

import pytest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "dags"))


class TestDagDefinition:
    """Tests for DAG definition."""

    def test_dag_is_valid(self):
        """Test that DAG is valid."""
        from dag_03_fill_analytics_marts import dag

        assert dag is not None
        assert dag.dag_id == "clickstream_03_fill_analytics_marts"

    def test_dag_has_correct_schedule(self):
        """Test DAG schedule (daily at 01:15)."""
        from dag_03_fill_analytics_marts import dag

        assert dag.schedule == "15 1 * * *"

    def test_dag_has_required_tasks(self):
        """Test that DAG has all required tasks."""
        from dag_03_fill_analytics_marts import dag

        task_ids = [task.task_id for task in dag.tasks]

        assert "ensure_state" in task_ids
        assert "fill_analytics_marts" in task_ids

    def test_dag_task_dependencies(self):
        """Test task dependencies."""
        from dag_03_fill_analytics_marts import dag

        tasks = {task.task_id: task for task in dag.tasks}

        # t0 >> t1
        assert "fill_analytics_marts" in [t.task_id for t in tasks["ensure_state"].downstream_list]

    def test_dag_has_correct_tags(self):
        """Test DAG tags."""
        from dag_03_fill_analytics_marts import dag

        assert "clickstream" in dag.tags
        assert "analytics" in dag.tags
        assert "marts" in dag.tags

    def test_dag_default_args(self):
        """Test default args."""
        from dag_03_fill_analytics_marts import dag

        assert dag.default_args["owner"] == "clickstream"
        assert dag.default_args["retries"] == 2


class TestChExecute:
    """Tests for ch_execute function."""

    @patch("dag_03_fill_analytics_marts._get_client")
    def test_ch_execute_without_settings(self, mock_get_client):
        """Test SQL execution without settings."""
        from dag_03_fill_analytics_marts import ch_execute

        mock_client = MagicMock()
        mock_get_client.return_value = mock_client

        ch_execute("SELECT 1")

        mock_client.execute.assert_called_once_with("SELECT 1")

    @patch("dag_03_fill_analytics_marts._get_client")
    def test_ch_execute_with_settings(self, mock_get_client):
        """Test SQL execution with settings."""
        from dag_03_fill_analytics_marts import ch_execute

        mock_client = MagicMock()
        mock_get_client.return_value = mock_client

        ch_execute("SELECT 1", settings={"max_execution_time": 300})

        mock_client.execute.assert_called_once_with(
            "SELECT 1", settings={"max_execution_time": 300}
        )


class TestChQuery:
    """Tests for ch_query function."""

    @patch("dag_03_fill_analytics_marts._get_client")
    def test_ch_query_returns_result(self, mock_get_client):
        """Test query returns result."""
        from dag_03_fill_analytics_marts import ch_query

        mock_client = MagicMock()
        mock_client.execute.return_value = [(1, "test")]
        mock_get_client.return_value = mock_client

        result = ch_query("SELECT 1, 'test'")

        assert result == [(1, "test")]


class TestEnsureState:
    """Tests for ensure_state function."""

    @patch("dag_03_fill_analytics_marts.ch_execute")
    def test_ensure_state_inserts_watermark(self, mock_ch_execute):
        """Test that watermark is inserted."""
        from dag_03_fill_analytics_marts import ensure_state

        ensure_state()

        mock_ch_execute.assert_called_once()
        sql = mock_ch_execute.call_args[0][0]
        assert "INSERT INTO clickstream.etl_state" in sql
        assert "fill_analytics_marts" in sql


class TestFillAnalyticsMarts:
    """Tests for fill_analytics_marts function."""

    @patch("dag_03_fill_analytics_marts.ch_execute")
    @patch("dag_03_fill_analytics_marts.ch_query")
    def test_fill_marts_handles_empty_prepared_events(self, mock_ch_query, mock_ch_execute):
        """Test handling when prepared_events is empty."""
        from dag_03_fill_analytics_marts import fill_analytics_marts

        mock_ch_query.side_effect = [
            [(datetime(2024, 1, 1),)],       # last_ts
            [(datetime(2024, 1, 2),)],       # upper_ts
            [(date(2024, 1, 1),)],           # end_day
            [(date(2023, 9, 3),)],           # start_day_candidate
            [(None,)],                       # min_day_prepared (empty)
        ]

        fill_analytics_marts()

        # Should only update watermark
        assert mock_ch_execute.call_count == 1
        sql = mock_ch_execute.call_args[0][0]
        assert "UPDATE last_ts" in sql

    @patch("dag_03_fill_analytics_marts.ch_execute")
    @patch("dag_03_fill_analytics_marts.ch_query")
    def test_fill_marts_handles_start_after_end(self, mock_ch_query, mock_ch_execute):
        """Test handling when start_day > end_day."""
        from dag_03_fill_analytics_marts import fill_analytics_marts

        mock_ch_query.side_effect = [
            [(datetime(2024, 1, 1),)],       # last_ts
            [(datetime(2024, 1, 2),)],       # upper_ts
            [(date(2024, 1, 1),)],           # end_day
            [(date(2024, 1, 10),)],          # start_day_candidate (after end_day)
            [(date(2024, 2, 1),)],           # min_day_prepared (also after end_day)
        ]

        fill_analytics_marts()

        # Should only update watermark
        assert mock_ch_execute.call_count == 1

    @patch("dag_03_fill_analytics_marts.ch_execute")
    @patch("dag_03_fill_analytics_marts.ch_query")
    def test_fill_marts_creates_all_tables(self, mock_ch_query, mock_ch_execute):
        """Test that all analytics tables are populated."""
        from dag_03_fill_analytics_marts import fill_analytics_marts

        mock_ch_query.side_effect = [
            [(datetime(2024, 1, 1),)],       # last_ts
            [(datetime(2024, 1, 2),)],       # upper_ts
            [(date(2024, 1, 15),)],          # end_day
            [(date(2023, 9, 17),)],          # start_day_candidate
            [(date(2024, 1, 1),)],           # min_day_prepared
        ]

        fill_analytics_marts()

        # Should have 6 executes: 5 mart inserts + 1 watermark update
        assert mock_ch_execute.call_count == 6

        sql_calls = [call[0][0] for call in mock_ch_execute.call_args_list]

        assert any("analytics.kpi_daily" in sql for sql in sql_calls)
        assert any("analytics.pages_daily" in sql for sql in sql_calls)
        assert any("analytics.session_quality_daily" in sql for sql in sql_calls)
        assert any("analytics.referrer_daily" in sql for sql in sql_calls)
        assert any("analytics.user_cohorts_daily" in sql for sql in sql_calls)

    @patch("dag_03_fill_analytics_marts.ch_execute")
    @patch("dag_03_fill_analytics_marts.ch_query")
    def test_fill_marts_kpi_daily(self, mock_ch_query, mock_ch_execute):
        """Test KPI daily mart insertion."""
        from dag_03_fill_analytics_marts import fill_analytics_marts

        mock_ch_query.side_effect = [
            [(datetime(2024, 1, 1),)],
            [(datetime(2024, 1, 2),)],
            [(date(2024, 1, 15),)],
            [(date(2023, 9, 17),)],
            [(date(2024, 1, 1),)],
        ]

        fill_analytics_marts()

        kpi_sql = mock_ch_execute.call_args_list[0][0][0]
        assert "INSERT INTO analytics.kpi_daily" in kpi_sql
        assert "events" in kpi_sql
        assert "users" in kpi_sql
        assert "sessions" in kpi_sql
        assert "ctr" in kpi_sql
        assert "conversion" in kpi_sql

    @patch("dag_03_fill_analytics_marts.ch_execute")
    @patch("dag_03_fill_analytics_marts.ch_query")
    def test_fill_marts_session_quality(self, mock_ch_query, mock_ch_execute):
        """Test session quality daily mart insertion."""
        from dag_03_fill_analytics_marts import fill_analytics_marts

        mock_ch_query.side_effect = [
            [(datetime(2024, 1, 1),)],
            [(datetime(2024, 1, 2),)],
            [(date(2024, 1, 15),)],
            [(date(2023, 9, 17),)],
            [(date(2024, 1, 1),)],
        ]

        fill_analytics_marts()

        session_sql = mock_ch_execute.call_args_list[2][0][0]
        assert "INSERT INTO analytics.session_quality_daily" in session_sql
        assert "bounce_rate" in session_sql

    @patch("dag_03_fill_analytics_marts.ch_execute")
    @patch("dag_03_fill_analytics_marts.ch_query")
    def test_fill_marts_user_cohorts(self, mock_ch_query, mock_ch_execute):
        """Test user cohorts daily mart insertion."""
        from dag_03_fill_analytics_marts import fill_analytics_marts

        mock_ch_query.side_effect = [
            [(datetime(2024, 1, 1),)],
            [(datetime(2024, 1, 2),)],
            [(date(2024, 1, 15),)],
            [(date(2023, 9, 17),)],
            [(date(2024, 1, 1),)],
        ]

        fill_analytics_marts()

        cohorts_sql = mock_ch_execute.call_args_list[4][0][0]
        assert "INSERT INTO analytics.user_cohorts_daily" in cohorts_sql
        assert "new_users" in cohorts_sql
        assert "returning_users" in cohorts_sql
        assert "heavy_users" in cohorts_sql

    @patch("dag_03_fill_analytics_marts.ch_execute")
    @patch("dag_03_fill_analytics_marts.ch_query")
    def test_fill_marts_updates_watermark(self, mock_ch_query, mock_ch_execute):
        """Test watermark update after processing."""
        from dag_03_fill_analytics_marts import fill_analytics_marts

        mock_ch_query.side_effect = [
            [(datetime(2024, 1, 1),)],
            [(datetime(2024, 1, 2),)],
            [(date(2024, 1, 15),)],
            [(date(2023, 9, 17),)],
            [(date(2024, 1, 1),)],
        ]

        fill_analytics_marts()

        last_sql = mock_ch_execute.call_args_list[-1][0][0]
        assert "ALTER TABLE clickstream.etl_state" in last_sql
        assert "UPDATE last_ts" in last_sql
