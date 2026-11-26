CREATE TABLE IF NOT EXISTS backup_history (
    id BIGINT GENERATED ALWAYS AS IDENTITY PRIMARY KEY,
    job_id BIGINT NOT NULL REFERENCES backup_jobs(id),
    started_at TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP,
    finished_at TIMESTAMPTZ,
    status VARCHAR(20) NOT NULL DEFAULT 'running',
    backup_path TEXT,
    size_bytes BIGINT,
    error_message TEXT
);