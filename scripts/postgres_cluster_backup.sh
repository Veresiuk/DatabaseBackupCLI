#!/usr/bin/env bash
set -euo pipefail

root="/backups/postgres-cluster"
mode="${1:-}"

if [[ "$mode" != "full" && "$mode" != "incremental" ]]; then
    echo "Usage: postgres_cluster_backup.sh full|incremental" >&2
    exit 2

fi

mkdir -p "$root"

stamp="$(date -u +%Y%m%dT%H%M%SZ)"
backup_id="${mode}_${stamp}"
backup_dir="$root/$backup_id"
parent_id=""

if [[ "$mode" == "incremental" ]]; then
    latest_file="$root/latest"

    if [[ ! -f "$latest_file" ]]; then
        echo "No previous cluster backup. Create a full backup first." >&2
        exit 1
    fi

    parent_id="$(<"$latest_file")"

    if [[ ! "$parent_id" =~ ^(full|incremental)_[0-9]{8}T[0-9]{6}Z$ ]]; then
        echo "Invalid latest backup marker." >&2
        exit 1
    fi

    parent_dir="$root/$parent_id"

    if [[ ! -f "$parent_dir/backup_manifest" ]]; then
        echo "Previous backup manifest is missing: $parent_id" >&2
        exit 1
    fi

    PGPASSWORD="${POSTGRES_PASSWORD:?POSTGRES_PASSWORD is not set}" \
        pg_basebackup \
        -h 127.0.0.1 \
        -U "${POSTGRES_USER:-backup_user}" \
        -D "$backup_dir" \
        -Fp -X stream -P \
        --incremental="$parent_dir/backup_manifest"
else
    PGPASSWORD="${POSTGRES_PASSWORD:?POSTGRES_PASSWORD is not set}" \
        pg_basebackup \
        -h 127.0.0.1 \
        -U "${POSTGRES_USER:-backup_user}" \
        -D "$backup_dir" \
        -Fp -X stream -P
fi

if [[ ! -f "$backup_dir/backup_manifest" ]]; then
    echo "Backup finished without a manifest." >&2
    exit 1
fi

echo "Verifying PostgreSQL cluster backup..."
if ! pg_verifybackup "$backup_dir"; then
    failed_dir="$root/failed_$backup_id"
    mv "$backup_dir" "$failed_dir"
    echo "Backup verification failed; moved it to $failed_dir" >&2
    exit 1
fi

if [[ -n "$parent_id" ]]; then
    mkdir -p "$root/.chains"
    printf '%s\n' "$parent_id" > "$root/.chains/$backup_id.parent"
fi

printf '%s\n' "$backup_id" > "$root/latest.tmp"
mv "$root/latest.tmp" "$root/latest"

echo "PostgreSQL cluster $mode backup created: $backup_id"