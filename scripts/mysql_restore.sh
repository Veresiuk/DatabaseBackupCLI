#!/bin/sh
set -eu

target_database="${1:?target database is required}"
archive="${2:?archive path is required}"

case "$target_database" in
    ""|*[!A-Za-z0-9_]*)
        echo "Invalid MySQL database name." >&2
        exit 2
        ;;
esac

case "$archive" in
    /backups/*.sql.gz) ;;
    *)
        echo "Archive must be a .sql.gz file inside /backups." >&2
        exit 2
        ;;
esac

temporary="${archive}.sql.tmp"
trap 'rm -f "$temporary"' EXIT

gzip -dc "$archive" > "$temporary"
MYSQL_PWD="$MYSQL_PASSWORD" mysql \
    --user="$MYSQL_USER" \
    "$target_database" < "$temporary"