#!/bin/sh
set -eu

database="${1:?database name is required}"
output="${2:?output path is required}"

case "$database" in
    ""|*[!A-Za-z0-9_]*)
        echo "Invalid MySQL database name." >&2
        exit 2
        ;;
esac

case "$output" in
    /backups/*) ;;
    *)
        echo "Output must be inside /backups." >&2
        exit 2
        ;;
esac

temporary="${output}.sql.tmp"
trap 'rm -f "$temporary"' EXIT

MYSQL_PWD="$MYSQL_PASSWORD" mysqldump \
    --user="$MYSQL_USER" \
    --single-transaction \
    --no-tablespaces \
    --routines \
    --events \
    --triggers \
    "$database" > "$temporary"

gzip -c "$temporary" > "$output"