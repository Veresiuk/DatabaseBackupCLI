#!/usr/bin/env bash
set -euo pipefail

root="/backups/postgres-cluster"
backup_id="${1:-}"

if [[ ! "$backup_id" =~ ^(full|incremental)_[0-9]{8}T[0-9]{6}Z$ ]]; then
    echo "Usage: postgres_cluster_combine.sh <backup-id>" >&2
    exit 2
fi

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

for ((i=${#chain[@]} - 1; i >= 0; --i)); do
    ordered+=("${chain[i]}")
done

output="$root/combined_$backup_id"
if [[ -e "$output" ]]; then
    echo "Output already exists: $output" >&2
    exit 1
fi

pg_combinebackup -o "$output" "${ordered[@]}"
echo "Combined cluster backup is ready: $output"