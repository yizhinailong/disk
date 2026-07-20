\set ON_ERROR_STOP on

BEGIN;

\ir schema_reversal_guard.sql

DO $rollback$
DECLARE
    has_rows BOOLEAN := FALSE;
    has_distributed_state BOOLEAN := FALSE;
    has_chunk_metadata BOOLEAN := FALSE;
BEGIN
    IF to_regclass('public.storage_jobs') IS NOT NULL THEN
        EXECUTE 'SELECT EXISTS (SELECT 1 FROM storage_jobs)' INTO has_rows;
        IF has_rows THEN
            RAISE EXCEPTION 'V003 rollback blocked: storage_jobs contains records';
        END IF;
    END IF;

    IF EXISTS (
        SELECT 1 FROM information_schema.columns
        WHERE table_schema = 'public'
          AND table_name = 'upload_tasks'
          AND column_name = 'staging_backend'
    ) THEN
        EXECUTE $query$
            SELECT EXISTS (
                SELECT 1 FROM upload_tasks
                WHERE status IN (4, 5)
                   OR staging_backend <> 'local'
                   OR state_version <> 0
                   OR finalize_attempts <> 0
                   OR lease_owner IS NOT NULL
                   OR lease_expires_at IS NOT NULL
                   OR last_error_code IS NOT NULL
                   OR last_error_at IS NOT NULL
                   OR completed_file_id IS NOT NULL
            )
        $query$ INTO has_distributed_state;
        IF has_distributed_state THEN
            RAISE EXCEPTION 'V003 rollback blocked: distributed upload state is in use';
        END IF;
    END IF;

    IF EXISTS (
        SELECT 1 FROM information_schema.columns
        WHERE table_schema = 'public'
          AND table_name = 'upload_task_chunks'
          AND column_name = 'size_bytes'
    ) THEN
        EXECUTE $query$
            SELECT EXISTS (
                SELECT 1 FROM upload_task_chunks
                WHERE size_bytes IS NOT NULL
                   OR hash_md5 IS NOT NULL
                   OR object_key IS NOT NULL
                   OR etag IS NOT NULL
            )
        $query$ INTO has_chunk_metadata;
        IF has_chunk_metadata THEN
            RAISE EXCEPTION 'V003 rollback blocked: distributed chunk metadata is in use';
        END IF;
    END IF;
END
$rollback$;

DROP TABLE IF EXISTS storage_jobs;

DROP INDEX IF EXISTS idx_upload_tasks_finalizing_lease;
DROP INDEX IF EXISTS idx_upload_tasks_staging_backend;

ALTER TABLE upload_task_chunks
    DROP CONSTRAINT IF EXISTS ck_upload_task_chunks_hash,
    DROP CONSTRAINT IF EXISTS ck_upload_task_chunks_size,
    DROP CONSTRAINT IF EXISTS ck_upload_task_chunks_index,
    DROP COLUMN IF EXISTS etag,
    DROP COLUMN IF EXISTS object_key,
    DROP COLUMN IF EXISTS hash_md5,
    DROP COLUMN IF EXISTS size_bytes;

ALTER TABLE upload_tasks
    DROP CONSTRAINT IF EXISTS fk_upload_tasks_completed_file_id,
    DROP CONSTRAINT IF EXISTS ck_upload_tasks_completed_file,
    DROP CONSTRAINT IF EXISTS ck_upload_tasks_finalizing_lease,
    DROP CONSTRAINT IF EXISTS ck_upload_tasks_staging_prefix,
    DROP CONSTRAINT IF EXISTS ck_upload_tasks_nonnegative,
    DROP CONSTRAINT IF EXISTS ck_upload_tasks_staging_backend,
    DROP CONSTRAINT IF EXISTS ck_upload_tasks_status,
    DROP COLUMN IF EXISTS completed_file_id,
    DROP COLUMN IF EXISTS last_error_at,
    DROP COLUMN IF EXISTS last_error_code,
    DROP COLUMN IF EXISTS finalize_attempts,
    DROP COLUMN IF EXISTS lease_expires_at,
    DROP COLUMN IF EXISTS lease_owner,
    DROP COLUMN IF EXISTS state_version,
    DROP COLUMN IF EXISTS staging_prefix,
    DROP COLUMN IF EXISTS staging_backend;

DO $rollback$
BEGIN
    IF to_regclass('public.schema_migrations') IS NOT NULL THEN
        DELETE FROM schema_migrations WHERE version = 'V003_distributed_upload';
    END IF;
END
$rollback$;

COMMIT;
