#!/bin/sh
set -eu

source_database="${1:?source database is required}"
target_database="${2:?target database is required}"
archive="${3:?archive path is required}"

for database in "$source_database" "$target_database"; do
    case "$database" in
        ""|*[!A-Za-z0-9_]*)
            echo "Invalid MongoDB database name." >&2
            exit 2
            ;;
    esac
done

case "$archive" in
    /backups/*.archive.gz) ;;
    *)
        echo "Archive must be a .archive.gz file inside /backups." >&2
        exit 2
        ;;
esac

mongorestore \
    --uri="$MONGO_URI" \
    --gzip \
    --archive="$archive" \
    --nsFrom="${source_database}.*" \
    --nsTo="${target_database}.*"