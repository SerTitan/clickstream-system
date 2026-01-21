#!/bin/bash
# =============================================================================
# Запуск SQL скриптов в ClickHouse
# Использование: ./run_sql.sh <script_name>
# Примеры:
#   ./run_sql.sh data_quality
#   ./run_sql.sh health_check
#   ./run_sql.sh sample_data
# =============================================================================

SCRIPT_DIR="$(dirname "$0")/sql"
CLICKHOUSE_HOST="${CLICKHOUSE_HOST:-localhost}"
CLICKHOUSE_PORT="${CLICKHOUSE_PORT:-8123}"
CLICKHOUSE_USER="${CLICKHOUSE_USER:-clickstream}"
CLICKHOUSE_PASSWORD="${CLICKHOUSE_PASSWORD:-clickstream123}"

if [ -z "$1" ]; then
    echo "Доступные скрипты:"
    ls -1 "$SCRIPT_DIR"/*.sql 2>/dev/null | xargs -n1 basename | sed 's/.sql$//'
    echo ""
    echo "Использование: $0 <script_name>"
    exit 1
fi

SQL_FILE="$SCRIPT_DIR/$1.sql"

if [ ! -f "$SQL_FILE" ]; then
    echo "Ошибка: файл $SQL_FILE не найден"
    exit 1
fi

echo "Запуск $SQL_FILE..."
echo "========================================"

# Разделяем SQL файл на отдельные запросы и выполняем их последовательно
current_query=""
query_num=0

while IFS= read -r line || [ -n "$line" ]; do
    # Выводим комментарии как заголовки секций
    if [[ "$line" =~ ^[[:space:]]*--[[:space:]]*[0-9]+\. ]]; then
        echo ""
        echo "$line"
        echo ""
        continue
    fi

    # Пропускаем строки-комментарии
    if [[ "$line" =~ ^[[:space:]]*-- ]]; then
        continue
    fi

    # Пропускаем пустые строки
    if [[ -z "${line// }" ]]; then
        continue
    fi

    # Добавляем строку к текущему запросу
    current_query+="$line"$'\n'

    # Если строка заканчивается на точку с запятой - выполняем запрос
    if [[ "$line" =~ \;[[:space:]]*$ ]]; then
        # Удаляем финальную точку с запятой
        query_to_run="${current_query%;*}"

        if [ -n "${query_to_run// }" ]; then
            ((query_num++))
            result=$(curl -s "http://$CLICKHOUSE_HOST:$CLICKHOUSE_PORT/" \
                --user "$CLICKHOUSE_USER:$CLICKHOUSE_PASSWORD" \
                --data "$query_to_run" \
                -H "X-ClickHouse-Format: PrettyCompact" 2>&1)

            if [ -n "$result" ]; then
                echo "$result"
            fi
        fi

        current_query=""
    fi
done < "$SQL_FILE"

echo ""
echo "========================================"
echo "Готово! Выполнено запросов: $query_num"
