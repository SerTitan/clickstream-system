-- =============================================================================
-- Superset Dashboard SQL Queries for Clickstream Analytics
-- =============================================================================
-- Database: ClickHouse (clickstream / analytics schemas)
-- Connection string: clickhouse+native://clickstream:clickstream123@clickhouse-1:9000/analytics
-- =============================================================================

-- =============================================================================
-- 1. DAU / WAU / MAU (Daily/Weekly/Monthly Active Users)
-- =============================================================================

-- Daily Active Users (DAU) - Time Series
SELECT
    day,
    users AS dau
FROM analytics.kpi_daily
WHERE day >= today() - 30
ORDER BY day;

-- Weekly Active Users (WAU) - Rolling 7 days
SELECT
    day,
    uniqExact(user_id) AS wau
FROM clickstream.prepared_events
WHERE event_time >= today() - 37
GROUP BY toDate(event_time) AS day
HAVING day >= today() - 30
ORDER BY day;

-- Monthly Active Users (MAU) - Rolling 30 days
SELECT
    day,
    uniqExact(user_id) AS mau
FROM clickstream.prepared_events
WHERE event_time >= today() - 60
GROUP BY toDate(event_time) AS day
HAVING day >= today() - 30
ORDER BY day;

-- Combined DAU/WAU/MAU Chart
WITH
    daily AS (
        SELECT
            toDate(event_time) AS day,
            uniqExact(user_id) AS dau
        FROM clickstream.prepared_events
        WHERE event_time >= today() - 30
        GROUP BY day
    )
SELECT
    d.day,
    d.dau,
    (SELECT uniqExact(user_id) FROM clickstream.prepared_events WHERE event_time >= d.day - 6 AND event_time < d.day + 1) AS wau,
    (SELECT uniqExact(user_id) FROM clickstream.prepared_events WHERE event_time >= d.day - 29 AND event_time < d.day + 1) AS mau
FROM daily d
ORDER BY d.day;

-- =============================================================================
-- 2. Click-to-View Ratio (CTR) - Heatmap / Line Chart
-- =============================================================================

-- CTR by Day
SELECT
    day,
    views,
    clicks,
    ctr AS click_to_view_ratio
FROM analytics.kpi_daily
WHERE day >= today() - 30
ORDER BY day;

-- CTR by Page (Top 20)
SELECT
    page_url,
    sum(views) AS total_views,
    sum(clicks) AS total_clicks,
    if(sum(views) > 0, sum(clicks) / sum(views), 0) AS ctr
FROM analytics.pages_daily
WHERE day >= today() - 30
GROUP BY page_url
ORDER BY total_views DESC
LIMIT 20;

-- CTR Heatmap by Day and Hour
SELECT
    toDate(event_time) AS day,
    toHour(event_time) AS hour,
    countIf(event_type = 'view') AS views,
    countIf(event_type = 'click') AS clicks,
    if(countIf(event_type = 'view') > 0, countIf(event_type = 'click') / countIf(event_type = 'view'), 0) AS ctr
FROM clickstream.prepared_events
WHERE event_time >= today() - 7
GROUP BY day, hour
ORDER BY day, hour;

-- =============================================================================
-- 3. Conversion Rate (Click to Purchase)
-- =============================================================================

-- Daily Conversion Rate
SELECT
    day,
    clicks,
    purchases,
    conversion AS conversion_rate
FROM analytics.kpi_daily
WHERE day >= today() - 30
ORDER BY day;

-- Funnel: View -> Click -> Purchase
SELECT
    toDate(event_time) AS day,
    countIf(event_type = 'view') AS views,
    countIf(event_type = 'click') AS clicks,
    countIf(event_type = 'purchase') AS purchases,
    if(views > 0, clicks / views, 0) AS view_to_click,
    if(clicks > 0, purchases / clicks, 0) AS click_to_purchase
FROM clickstream.prepared_events
WHERE event_time >= today() - 30
GROUP BY day
ORDER BY day;

-- =============================================================================
-- 4. Average Session Duration
-- =============================================================================

-- Session Duration by Day
SELECT
    day,
    sessions,
    avg_duration_sec,
    median_duration_sec,
    bounce_rate
FROM analytics.session_quality_daily
WHERE day >= today() - 30
ORDER BY day;

-- Detailed Session Stats
SELECT
    toDate(min_time) AS day,
    count() AS sessions,
    avg(duration_sec) AS avg_duration,
    median(duration_sec) AS median_duration,
    countIf(events_count = 1) / count() AS bounce_rate
FROM (
    SELECT
        session_id,
        min(event_time) AS min_time,
        max(event_time) AS max_time,
        dateDiff('second', min(event_time), max(event_time)) AS duration_sec,
        count() AS events_count
    FROM clickstream.prepared_events
    WHERE event_time >= today() - 30
    GROUP BY session_id
)
GROUP BY day
ORDER BY day;

-- =============================================================================
-- 5. Device Type Distribution
-- =============================================================================

-- Device Type Pie Chart (from user_agent patterns)
SELECT
    CASE
        WHEN user_agent LIKE '%Mobile%' OR user_agent LIKE '%Android%' AND user_agent NOT LIKE '%Tablet%' THEN 'Mobile'
        WHEN user_agent LIKE '%iPad%' OR user_agent LIKE '%Tablet%' THEN 'Tablet'
        ELSE 'Desktop'
    END AS device_type,
    count() AS events,
    uniqExact(user_id) AS users
FROM clickstream.prepared_events
WHERE event_time >= today() - 30
GROUP BY device_type
ORDER BY events DESC;

-- Device Type Trend
SELECT
    toDate(event_time) AS day,
    CASE
        WHEN user_agent LIKE '%Mobile%' OR user_agent LIKE '%Android%' AND user_agent NOT LIKE '%Tablet%' THEN 'Mobile'
        WHEN user_agent LIKE '%iPad%' OR user_agent LIKE '%Tablet%' THEN 'Tablet'
        ELSE 'Desktop'
    END AS device_type,
    count() AS events
FROM clickstream.prepared_events
WHERE event_time >= today() - 14
GROUP BY day, device_type
ORDER BY day, device_type;

-- =============================================================================
-- 6. Top 10 Pages
-- =============================================================================

-- Top 10 Pages by Views
SELECT
    page_url,
    sum(views) AS total_views,
    sum(clicks) AS total_clicks,
    sum(users) AS unique_users,
    if(sum(views) > 0, sum(clicks) / sum(views), 0) AS ctr
FROM analytics.pages_daily
WHERE day >= today() - 30
GROUP BY page_url
ORDER BY total_views DESC
LIMIT 10;

-- Top 10 Pages by Unique Users
SELECT
    page_url,
    uniqExact(user_id) AS unique_users,
    count() AS total_events,
    countIf(event_type = 'view') AS views,
    countIf(event_type = 'click') AS clicks
FROM clickstream.prepared_events
WHERE event_time >= today() - 30
GROUP BY page_url
ORDER BY unique_users DESC
LIMIT 10;

-- =============================================================================
-- 7. Traffic Sources (Referrers)
-- =============================================================================

-- Top Referrers
SELECT
    referrer,
    sum(events) AS total_events,
    sum(users) AS unique_users,
    sum(views) AS views,
    sum(clicks) AS clicks,
    avg(traffic_share) AS avg_traffic_share
FROM analytics.referrer_daily
WHERE day >= today() - 30
  AND referrer != ''
GROUP BY referrer
ORDER BY total_events DESC
LIMIT 10;

-- Direct vs Referral Traffic
SELECT
    if(referrer = '', 'Direct', 'Referral') AS traffic_type,
    count() AS events,
    uniqExact(user_id) AS users,
    uniqExact(session_id) AS sessions
FROM clickstream.prepared_events
WHERE event_time >= today() - 30
GROUP BY traffic_type;

-- =============================================================================
-- 8. User Cohorts
-- =============================================================================

-- New vs Returning Users by Day
SELECT
    day,
    new_users,
    returning_users,
    new_share
FROM analytics.user_cohorts_daily
WHERE day >= today() - 30
ORDER BY day;

-- User Activity Segments
SELECT
    day,
    heavy_users,
    medium_users,
    light_users
FROM analytics.user_cohorts_daily
WHERE day >= today() - 30
ORDER BY day;

-- =============================================================================
-- 9. Real-time Metrics (Last Hour)
-- =============================================================================

-- Events in Last Hour by Minute
SELECT
    toStartOfMinute(event_time) AS minute,
    count() AS events,
    uniqExact(user_id) AS users,
    countIf(event_type = 'view') AS views,
    countIf(event_type = 'click') AS clicks
FROM clickstream.prepared_events
WHERE event_time >= now() - INTERVAL 1 HOUR
GROUP BY minute
ORDER BY minute;

-- Current Active Users (Last 5 minutes)
SELECT
    uniqExact(user_id) AS active_users,
    uniqExact(session_id) AS active_sessions,
    count() AS events
FROM clickstream.prepared_events
WHERE event_time >= now() - INTERVAL 5 MINUTE;

-- =============================================================================
-- 10. Data Quality Metrics
-- =============================================================================

-- Invalid Events by Day
SELECT
    day,
    events,
    invalid_events,
    invalid_share
FROM analytics.kpi_daily
WHERE day >= today() - 30
ORDER BY day;

-- Invalid Events by Error Type
SELECT
    toDate(received_time) AS day,
    error_code,
    count() AS count
FROM clickstream.invalid_events
WHERE received_time >= today() - 7
GROUP BY day, error_code
ORDER BY day, count DESC;

-- =============================================================================
-- 11. Element Click Heatmap
-- =============================================================================

-- Click Heatmap by Element ID
SELECT
    props_element_id AS element_id,
    count() AS clicks,
    uniqExact(user_id) AS unique_users,
    avg(props_x) AS avg_x,
    avg(props_y) AS avg_y
FROM clickstream.prepared_events
WHERE event_type = 'click'
  AND event_time >= today() - 7
  AND props_element_id != ''
GROUP BY element_id
ORDER BY clicks DESC
LIMIT 20;

-- Click Distribution by Coordinates (for heatmap visualization)
SELECT
    floor(props_x / 50) * 50 AS x_bucket,
    floor(props_y / 50) * 50 AS y_bucket,
    count() AS clicks
FROM clickstream.prepared_events
WHERE event_type = 'click'
  AND event_time >= today() - 7
GROUP BY x_bucket, y_bucket
ORDER BY clicks DESC;
