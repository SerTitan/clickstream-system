-- =============================================================================
-- Примеры данных из таблиц (для отладки)
-- =============================================================================

-- 1. Последние 5 сырых событий
SELECT '=== RAW EVENTS (последние 5) ===' AS section;

SELECT *
FROM clickstream.raw_events
ORDER BY received_at DESC
LIMIT 5;

-- 2. Последние 5 подготовленных событий
SELECT '=== PREPARED EVENTS (последние 5) ===' AS section;

SELECT *
FROM clickstream.prepared_events
ORDER BY event_time DESC
LIMIT 5;

-- 3. Последние невалидные события (если есть)
SELECT '=== INVALID EVENTS (последние 5) ===' AS section;

SELECT *
FROM clickstream.invalid_events
ORDER BY received_time DESC
LIMIT 5;

-- 4. Пример из KPI витрины
SELECT '=== KPI DAILY (последние 5 дней) ===' AS section;

SELECT *
FROM analytics.kpi_daily
ORDER BY day DESC
LIMIT 5;

-- 5. Пример сессии пользователя
SELECT '=== ПРИМЕР СЕССИИ ===' AS section;

WITH sample_session AS (
    SELECT session_id
    FROM clickstream.prepared_events
    ORDER BY event_time DESC
    LIMIT 1
)
SELECT
    event_time,
    event_type,
    page_url,
    props_element_id
FROM clickstream.prepared_events
WHERE session_id IN (SELECT session_id FROM sample_session)
ORDER BY event_time;
