#!/bin/sh
set -eu

database="${1:?database name is required}"
output="${2:?output path is required}"

case "$database" in
    ""|*[!A-Za-z0-9_]*)
        echo "Invalid MongoDB database name." >&2
        exit 2
        ;;
esac

case "$output" in
    /backups/*.archive.gz) ;;
    *)
        echo "Output must be a .archive.gz file inside /backups." >&2
        exit 2
        ;;
esac

mongodump \
    --uri="$MONGO_URI" \
    --db="$database" \
    --archive="$output" \
    --gzip