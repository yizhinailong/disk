ALTER TABLE upload_tasks
    ADD COLUMN IF NOT EXISTS staging_backend VARCHAR(16) NOT NULL DEFAULT 'local',
    ADD COLUMN IF NOT EXISTS staging_prefix VARCHAR(1024) DEFAULT NULL,
    ADD COLUMN IF NOT EXISTS state_version BIGINT NOT NULL DEFAULT 0,
    ADD COLUMN IF NOT EXISTS lease_owner VARCHAR(128) DEFAULT NULL,
    ADD COLUMN IF NOT EXISTS lease_expires_at TIMESTAMP DEFAULT NULL,
    ADD COLUMN IF NOT EXISTS finalize_attempts INTEGER NOT NULL DEFAULT 0,
    ADD COLUMN IF NOT EXISTS last_error_code INTEGER DEFAULT NULL,
    ADD COLUMN IF NOT EXISTS last_error_at TIMESTAMP DEFAULT NULL,
    ADD COLUMN IF NOT EXISTS completed_file_id BIGINT DEFAULT NULL;

UPDATE upload_tasks
SET staging_prefix = 'staging/' || id
WHERE staging_prefix IS NULL;

DO $migration$
BEGIN
    IF NOT EXISTS (
        SELECT 1 FROM pg_constraint
        WHERE conname = 'ck_upload_tasks_status'
          AND conrelid = 'upload_tasks'::regclass
    ) THEN
        ALTER TABLE upload_tasks
            ADD CONSTRAINT ck_upload_tasks_status
            CHECK (status BETWEEN 0 AND 5) NOT VALID;
    END IF;

    IF NOT EXISTS (
        SELECT 1 FROM pg_constraint
        WHERE conname = 'ck_upload_tasks_staging_backend'
          AND conrelid = 'upload_tasks'::regclass
    ) THEN
        ALTER TABLE upload_tasks
            ADD CONSTRAINT ck_upload_tasks_staging_backend
            CHECK (staging_backend IN ('local', 's3')) NOT VALID;
    END IF;

    IF NOT EXISTS (
        SELECT 1 FROM pg_constraint
        WHERE conname = 'ck_upload_tasks_nonnegative'
          AND conrelid = 'upload_tasks'::regclass
    ) THEN
        ALTER TABLE upload_tasks
            ADD CONSTRAINT ck_upload_tasks_nonnegative
            CHECK (
                reserved_bytes >= 0
                AND state_version >= 0
                AND finalize_attempts >= 0
            ) NOT VALID;
    END IF;

    IF NOT EXISTS (
        SELECT 1 FROM pg_constraint
        WHERE conname = 'ck_upload_tasks_staging_prefix'
          AND conrelid = 'upload_tasks'::regclass
    ) THEN
        ALTER TABLE upload_tasks
            ADD CONSTRAINT ck_upload_tasks_staging_prefix
            CHECK (staging_backend <> 's3' OR staging_prefix IS NOT NULL) NOT VALID;
    END IF;

    IF NOT EXISTS (
        SELECT 1 FROM pg_constraint
        WHERE conname = 'ck_upload_tasks_finalizing_lease'
          AND conrelid = 'upload_tasks'::regclass
    ) THEN
        ALTER TABLE upload_tasks
            ADD CONSTRAINT ck_upload_tasks_finalizing_lease
            CHECK (
                (
                    status = 4
                    AND lease_owner IS NOT NULL
                    AND lease_expires_at IS NOT NULL
                )
                OR (
                    status <> 4
                    AND lease_owner IS NULL
                    AND lease_expires_at IS NULL
                )
            ) NOT VALID;
    END IF;

    IF NOT EXISTS (
        SELECT 1 FROM pg_constraint
        WHERE conname = 'ck_upload_tasks_completed_file'
          AND conrelid = 'upload_tasks'::regclass
    ) THEN
        ALTER TABLE upload_tasks
            ADD CONSTRAINT ck_upload_tasks_completed_file
            CHECK (completed_file_id IS NULL OR status = 1) NOT VALID;
    END IF;

    IF NOT EXISTS (
        SELECT 1 FROM pg_constraint
        WHERE conname = 'fk_upload_tasks_completed_file_id'
          AND conrelid = 'upload_tasks'::regclass
    ) THEN
        ALTER TABLE upload_tasks
            ADD CONSTRAINT fk_upload_tasks_completed_file_id
            FOREIGN KEY (completed_file_id) REFERENCES files (id) ON DELETE SET NULL;
    END IF;
END
$migration$;

ALTER TABLE upload_tasks VALIDATE CONSTRAINT ck_upload_tasks_status;
ALTER TABLE upload_tasks VALIDATE CONSTRAINT ck_upload_tasks_staging_backend;
ALTER TABLE upload_tasks VALIDATE CONSTRAINT ck_upload_tasks_nonnegative;
ALTER TABLE upload_tasks VALIDATE CONSTRAINT ck_upload_tasks_staging_prefix;
ALTER TABLE upload_tasks VALIDATE CONSTRAINT ck_upload_tasks_finalizing_lease;
ALTER TABLE upload_tasks VALIDATE CONSTRAINT ck_upload_tasks_completed_file;

CREATE INDEX IF NOT EXISTS idx_upload_tasks_finalizing_lease
    ON upload_tasks (lease_expires_at)
    WHERE status = 4;

CREATE INDEX IF NOT EXISTS idx_upload_tasks_staging_backend
    ON upload_tasks (staging_backend, status);

ALTER TABLE upload_task_chunks
    ADD COLUMN IF NOT EXISTS size_bytes BIGINT DEFAULT NULL,
    ADD COLUMN IF NOT EXISTS hash_md5 CHAR(32) DEFAULT NULL,
    ADD COLUMN IF NOT EXISTS object_key VARCHAR(1024) DEFAULT NULL,
    ADD COLUMN IF NOT EXISTS etag VARCHAR(256) DEFAULT NULL;

DO $migration$
BEGIN
    IF NOT EXISTS (
        SELECT 1 FROM pg_constraint
        WHERE conname = 'ck_upload_task_chunks_index'
          AND conrelid = 'upload_task_chunks'::regclass
    ) THEN
        ALTER TABLE upload_task_chunks
            ADD CONSTRAINT ck_upload_task_chunks_index
            CHECK (chunk_index >= 0) NOT VALID;
    END IF;

    IF NOT EXISTS (
        SELECT 1 FROM pg_constraint
        WHERE conname = 'ck_upload_task_chunks_size'
          AND conrelid = 'upload_task_chunks'::regclass
    ) THEN
        ALTER TABLE upload_task_chunks
            ADD CONSTRAINT ck_upload_task_chunks_size
            CHECK (size_bytes IS NULL OR size_bytes > 0) NOT VALID;
    END IF;

    IF NOT EXISTS (
        SELECT 1 FROM pg_constraint
        WHERE conname = 'ck_upload_task_chunks_hash'
          AND conrelid = 'upload_task_chunks'::regclass
    ) THEN
        ALTER TABLE upload_task_chunks
            ADD CONSTRAINT ck_upload_task_chunks_hash
            CHECK (hash_md5 IS NULL OR hash_md5 ~ '^[0-9a-f]{32}$') NOT VALID;
    END IF;
END
$migration$;

ALTER TABLE upload_task_chunks VALIDATE CONSTRAINT ck_upload_task_chunks_index;
ALTER TABLE upload_task_chunks VALIDATE CONSTRAINT ck_upload_task_chunks_size;
ALTER TABLE upload_task_chunks VALIDATE CONSTRAINT ck_upload_task_chunks_hash;

CREATE TABLE IF NOT EXISTS storage_jobs (
    id BIGINT GENERATED ALWAYS AS IDENTITY PRIMARY KEY,
    job_type VARCHAR(64) NOT NULL,
    aggregate_id VARCHAR(128) NOT NULL,
    dedupe_key VARCHAR(255) NOT NULL,
    payload JSONB NOT NULL DEFAULT '{}'::jsonb,
    status SMALLINT NOT NULL DEFAULT 0,
    attempts INTEGER NOT NULL DEFAULT 0,
    max_attempts INTEGER NOT NULL DEFAULT 8,
    available_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    locked_by VARCHAR(128) DEFAULT NULL,
    locked_until TIMESTAMP DEFAULT NULL,
    last_error VARCHAR(2048) DEFAULT NULL,
    created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    updated_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    completed_at TIMESTAMP DEFAULT NULL,
    CONSTRAINT uk_storage_jobs_dedupe_key UNIQUE (dedupe_key),
    CONSTRAINT ck_storage_jobs_status CHECK (status BETWEEN 0 AND 4),
    CONSTRAINT ck_storage_jobs_attempts CHECK (attempts >= 0 AND max_attempts > 0),
    CONSTRAINT ck_storage_jobs_running_lease CHECK (
        (
            status = 1
            AND locked_by IS NOT NULL
            AND locked_until IS NOT NULL
        )
        OR (
            status <> 1
            AND locked_by IS NULL
            AND locked_until IS NULL
        )
    )
);

CREATE INDEX IF NOT EXISTS idx_storage_jobs_ready
    ON storage_jobs (available_at, id)
    WHERE status IN (0, 2);

CREATE INDEX IF NOT EXISTS idx_storage_jobs_expired_lease
    ON storage_jobs (locked_until, id)
    WHERE status = 1;

CREATE INDEX IF NOT EXISTS idx_storage_jobs_type_status
    ON storage_jobs (job_type, status);

COMMENT ON COLUMN upload_tasks.staging_backend IS 'Task staging backend: local or s3';
COMMENT ON COLUMN upload_tasks.staging_prefix IS 'Credential-free object staging prefix';
COMMENT ON COLUMN upload_tasks.state_version IS 'Optimistic state and lease version';
COMMENT ON COLUMN upload_tasks.lease_owner IS 'Current finalization lease owner';
COMMENT ON COLUMN upload_tasks.lease_expires_at IS 'Finalization lease expiry using database time';
COMMENT ON COLUMN upload_tasks.finalize_attempts IS 'Finalization claim and takeover count';
COMMENT ON COLUMN upload_tasks.last_error_code IS 'Last finalization domain error code';
COMMENT ON COLUMN upload_tasks.last_error_at IS 'Last finalization error timestamp';
COMMENT ON COLUMN upload_tasks.completed_file_id IS 'File returned by idempotent complete replay';
COMMENT ON COLUMN upload_task_chunks.size_bytes IS 'Verified chunk object size';
COMMENT ON COLUMN upload_task_chunks.hash_md5 IS 'Verified chunk MD5';
COMMENT ON COLUMN upload_task_chunks.object_key IS 'Credential-free staging object key';
COMMENT ON COLUMN upload_task_chunks.etag IS 'Object-store version diagnostic, not a file MD5';
COMMENT ON TABLE storage_jobs IS 'Durable object storage and reconciliation jobs';
