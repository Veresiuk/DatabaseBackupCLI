#!/usr/bin/env bash
set -euo pipefail

root="/backups/postgres-cluster"
keep="${1:-4}"
mode="${2:-}"

if [[ ! "$keep" =~ ^[1-9][0-9]{0,2}$ ]]; then
    echo "Keep count must be a number from 1 to 999." >&2
    exit 2
fi

if [[ "$mode" != "" && "$mode" != "--apply" ]]; then
    echo "Usage: postgres_cluster_retention.sh [keep-full-count] [--apply]" >&2
    exit 2
fi

if [[ ! -d "$root" || ! -f "$root/latest" ]]; then
    echo "Cluster backup directory or latest marker is missing." >&2
    exit 1
fi

find_chain_root() {
    local current="$1"
    local backup_dir
    local parent_file
    declare -A visited=()

    while [[ "$current" == incremental_* ]]; do
        if [[ ! "$current" =~ ^incremental_[0-9]{8}T[0-9]{6}Z$ ]]; then
            echo "Invalid incremental backup ID: $current" >&2
            return 1
        fi

        if [[ -n "${visited[$current]:-}" ]]; then
            echo "Cycle detected in backup chain at $current." >&2
            return 1
        fi
        visited["$current"]=1

        backup_dir="$root/$current"
        if [[ ! -f "$backup_dir/backup_manifest" ]]; then
            echo "Backup manifest is missing: $current" >&2
            return 1
        fi

        parent_file="$root/.chains/$current.parent"
        if [[ -f "$parent_file" ]]; then
            current="$(<"$parent_file")"
        elif [[ -f "$backup_dir/parent" ]]; then
            # Accept metadata written by the older script version.
            current="$(<"$backup_dir/parent")"
        else
            echo "Parent marker is missing for $current." >&2
            return 1
        fi
    done

    if [[ ! "$current" =~ ^full_[0-9]{8}T[0-9]{6}Z$ ||
          ! -f "$root/$current/backup_manifest" ]]; then
        echo "Full backup or its manifest is missing: $current" >&2
        return 1
    fi

    printf '%s\n' "$current"
}

declare -a full_ids=()
for path in "$root"/full_*; do
    [[ -d "$path" ]] || continue
    id="${path##*/}"

    if [[ "$id" =~ ^full_[0-9]{8}T[0-9]{6}Z$ &&
          -f "$path/backup_manifest" ]]; then
        full_ids+=("$id")
    fi
done

if [[ "${#full_ids[@]}" -eq 0 ]]; then
    echo "No complete full cluster backups found." >&2
    exit 1
fi

mapfile -t full_ids < <(printf '%s\n' "${full_ids[@]}" | sort -r)

declare -A keep_roots=()
for ((i = 0; i < keep && i < ${#full_ids[@]}; ++i)); do
    keep_roots["${full_ids[i]}"]=1
done

latest_id="$(<"$root/latest")"
latest_root="$(find_chain_root "$latest_id")" || exit 1
keep_roots["$latest_root"]=1

declare -a remove_ids=()

for id in "${full_ids[@]}"; do
    if [[ -z "${keep_roots[$id]:-}" ]]; then
        remove_ids+=("$id")
    fi
done

for path in "$root"/incremental_*; do
    [[ -d "$path" ]] || continue
    id="${path##*/}"

    [[ "$id" =~ ^incremental_[0-9]{8}T[0-9]{6}Z$ ]] || continue
    [[ -f "$path/backup_manifest" ]] || continue

    chain_root="$(find_chain_root "$id")" || exit 1

    if [[ -z "${keep_roots[$chain_root]:-}" ]]; then
        remove_ids+=("$id")
    fi
done

if [[ "${#remove_ids[@]}" -eq 0 ]]; then
    echo "Nothing to remove. Keeping $keep full chain(s) and the latest chain."
    exit 0
fi

if [[ "$mode" != "--apply" ]]; then
    echo "DRY RUN: these cluster backup directories would be removed:"
    for id in "${remove_ids[@]}"; do
        printf '  %s\n' "$id"
    done
    echo "No files were deleted. Add --apply to perform this cleanup."
    exit 0
fi

for id in "${remove_ids[@]}"; do
    if [[ ! "$id" =~ ^(full|incremental)_[0-9]{8}T[0-9]{6}Z$ ]]; then
        echo "Refusing to remove unexpected path: $id" >&2
        exit 1
    fi

    echo "Removing $id"
    rm -rf -- "$root/$id"
    rm -f -- "$root/.chains/$id.parent"
done

echo "Cluster backup retention completed."