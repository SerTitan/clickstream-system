-- =============================================================================
-- Проверка качества данных в ClickHouse
-- =============================================================================

-- 1. Общая статистика по таблицам
SELECT '=== ОБЩАЯ СТАТИСТИКА ===' AS section;

SELECT
    'raw_events' AS table_name,
    count() AS total_rows,
    uniqExact(user_id) AS unique_users,
    uniqExact(session_id) AS unique_sessions,
    min(event_time) AS min_date,
    max(event_time) AS max_date
FROM clickstream.raw_events;

SELECT
    'prepared_events' AS table_name,
    count() AS total_rows,
    uniqExact(user_id) AS unique_users,
    uniqExact(session_id) AS unique_sessions,
    min(event_time) AS min_date,
    max(event_time) AS max_date
FROM clickstream.prepared_events;

SELECT
    'invalid_events' AS table_name,
    count() AS total_rows
FROM clickstream.invalid_events;

-- 2. Распределение по типам событий
SELECT '=== РАСПРЕДЕЛЕНИЕ ПО ТИПАМ ===' AS section;

SELECT
    event_type,
    count() AS cnt,
    round(count() * 100.0 / sum(count()) OVER (), 2) AS percent
FROM clickstream.prepared_events
GROUP BY event_type
ORDER BY cnt DESC;

-- 3. Распределение по устройствам
SELECT '=== РАСПРЕДЕЛЕНИЕ ПО УСТРОЙСТВАМ ===' AS section;

SELECT
    device_id AS device,
    count() AS cnt,
    round(count() * 100.0 / sum(count()) OVER (), 2) AS percent
FROM clickstream.prepared_events
GROUP BY device_id
ORDER BY cnt DESC;

-- 4. Топ-10 страниц
SELECT '=== ТОП-10 СТРАНИЦ ===' AS section;

SELECT
    page_url,
    count() AS events,
    uniqExact(user_id) AS users
FROM clickstream.prepared_events
GROUP BY page_url
ORDER BY events DESC
LIMIT 10;

-- 5. Топ-10 referrer
SELECT '=== ТОП-10 ИСТОЧНИКОВ ===' AS section;

SELECT
    referrer,
    count() AS events,
    uniqExact(user_id) AS users
FROM clickstream.prepared_events
WHERE referrer != ''
GROUP BY referrer
ORDER BY events DESC
LIMIT 10;

-- 6. Невалидные события (если есть)
SELECT '=== НЕВАЛИДНЫЕ СОБЫТИЯ ===' AS section;

SELECT
    error_code,
    count() AS cnt
FROM clickstream.invalid_events
GROUP BY error_code
ORDER BY cnt DESC
LIMIT 10;

-- 7. События по дням (последние 7 дней)
SELECT '=== СОБЫТИЯ ПО ДНЯМ ===' AS section;

SELECT
    toDate(event_time) AS day,
    count() AS events,
    uniqExact(user_id) AS users,
    uniqExact(session_id) AS sessions
FROM clickstream.prepared_events
WHERE event_time >= today() - 7
GROUP BY day
ORDER BY day DESC;

-- 8. Проверка витрин
SELECT '=== ВИТРИНЫ ===' AS section;

SELECT 'kpi_daily' AS mart, count() AS rows FROM analytics.kpi_daily;
SELECT 'pages_daily' AS mart, count() AS rows FROM analytics.pages_daily;
SELECT 'session_quality_daily' AS mart, count() AS rows FROM analytics.session_quality_daily;
SELECT 'referrer_daily' AS mart, count() AS rows FROM analytics.referrer_daily;
SELECT 'user_cohorts_daily' AS mart, count() AS rows FROM analytics.user_cohorts_daily;
