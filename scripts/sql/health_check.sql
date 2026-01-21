-- =============================================================================
-- Быстрая проверка здоровья системы
-- =============================================================================

-- 1. Подсчет записей во всех таблицах
SELECT
    database,
    table,
    sum(rows) AS rows,
    formatReadableSize(sum(bytes_on_disk)) AS size
FROM system.parts
WHERE active AND database IN ('clickstream', 'analytics')
GROUP BY database, table
ORDER BY database, table;

-- 2. Последние события (проверка что данные поступают)
SELECT
    'raw_events' AS source,
    max(received_time) AS last_event,
    dateDiff('second', max(received_time), now()) AS seconds_ago
FROM clickstream.raw_events;

SELECT
    'prepared_events' AS source,
    max(event_time) AS last_event,
    dateDiff('second', max(event_time), now()) AS seconds_ago
FROM clickstream.prepared_events;

-- 3. Rate событий за последние 5 минут
SELECT
    count() AS events_5min,
    round(count() / 300, 2) AS events_per_sec
FROM clickstream.raw_events
WHERE received_time >= now() - INTERVAL 5 MINUTE;

-- 4. Процент невалидных событий
SELECT
    (SELECT count() FROM clickstream.invalid_events WHERE received_time >= today()) AS invalid_today,
    (SELECT count() FROM clickstream.raw_events WHERE received_time >= today()) AS total_today,
    round(invalid_today * 100.0 / nullIf(total_today, 0), 2) AS invalid_percent;
