# Superset для Clickstream Analytics

## Автоматическая инициализация

Дашборды создаются автоматически при запуске `docker-compose up -d`.

Superset сам запускает миграции, создает админа и накатывает дашборды через
`configs/superset/init_dashboards.py` после старта веб-сервера.

## Доступ

- URL: http://localhost:8088
- Логин: `admin` / `admin`
- Дашборд: http://localhost:8088/superset/dashboard/clickstream-analytics/

## Графики

| Название | Тип | Источник |
|----------|-----|----------|
| DAU | Line | kpi_daily |
| CTR | Line | kpi_daily |
| Конверсия | Line | kpi_daily |
| Длительность сессии | Bar | session_quality_daily |
| Топ-10 страниц | Table | pages_daily |
| Источники трафика | Pie | referrer_daily |
| Новые vs Вернувшиеся | Area | user_cohorts_daily |
| Bounce Rate | Big Number | session_quality_daily |

## Ручной запуск скрипта

```bash
docker compose exec superset python /app/init/init_dashboards.py
```
