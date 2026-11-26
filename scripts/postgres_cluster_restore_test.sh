#!/usr/bin/env bash
set -euo pipefail

root="/backups/postgres-cluster"
backup_id="${1:-}"

if [[ ! "$backup_id" =~ ^(full|incremental)_[0-9]{8}T[0-9]{6}Z$ ]]; then
    echo "Usage: postgres_cluster_restore_test.sh <backup-id>" >&2
    exit 2
fi

tmp_dir="$(mktemp -d /tmp/postgres-cluster-restore.XXXXXX)"
data_dir="$tmp_dir/data"
socket_dir="$tmp_dir/socket"
server_started=0
test_port=55432

cleanup() {
    if [[ "$server_started" == "1" ]]; then
            runuser -u postgres -- pg_ctl -D "$data_dir" stop -m fast -w || true
    fi

    rm -rf -- "$tmp_dir"
}
trap cleanup EXIT

mkdir -p "$socket_dir"

declare -a chain=()
declare -a ordered=()
declare -A seen=()

current="$backup_id"

while true; do
    if [[ ! "$current" =~ ^(full|incremental)_[0-9]{8}T[0-9]{6}Z$ ]]; then
        echo "Invalid backup dependency marker." >&2
        exit 1
    fi

    if [[ -n "${seen[$current]:-}" ]]; then
        echo "Cycle detected in backup chain." >&2
        exit 1
    fi
    seen["$current"]=1

    backup_dir="$root/$current"
    if [[ ! -f "$backup_dir/backup_manifest" ]]; then
        echo "Backup manifest is missing: $current" >&2
        exit 1
    fi

    chain+=("$backup_dir")

    if [[ "$current" == full_* ]]; then
        break
    fi

    parent_file="$root/.chains/$current.parent"

    if [[ -f "$parent_file" ]]; then
        current="$(<"$parent_file")"
    elif [[ -f "$backup_dir/parent" ]]; then
        current="$(<"$backup_dir/parent")"
    else
        echo "Parent marker is missing for $current." >&2
        exit 1
    fi
done

for ((i = ${#chain[@]} - 1; i >= 0; --i)); do
    ordered+=("${chain[i]}")
done

pg_combinebackup -o "$data_dir" "${ordered[@]}"
pg_verifybackup "$data_dir"

chown -R postgres:postgres "$tmp_dir"

if ! runuser -u postgres -- pg_ctl \
    -D "$data_dir" \
    -l "$tmp_dir/postgres.log" \
    -o "-p $test_port -c listen_addresses= -c unix_socket_directories=$socket_dir" \
    -w -t 60 start
then
    echo "Temporary PostgreSQL server failed to start. Log:" >&2
    cat "$tmp_dir/postgres.log" >&2 || true
    exit 1
fi

server_started=1

database_user="${POSTGRES_USER:-backup_user}"
database_name="${POSTGRES_DB:-backup_demo}"
database_password="${POSTGRES_PASSWORD:?POSTGRES_PASSWORD is not set}"

row_count="$(
    runuser -u postgres -- env PGPASSWORD="$database_password" psql \
        -X \
        -v ON_ERROR_STOP=1 \
        -h "$socket_dir" \
        -p "$test_port" \
        -U "$database_user" \
        -d "$database_name" \
        -Atqc "SELECT count(*) FROM public.backup_jobs;"
)"

echo "Cluster restore test succeeded."
echo "Database: $database_name"
echo "backup_jobs rows: $row_count"