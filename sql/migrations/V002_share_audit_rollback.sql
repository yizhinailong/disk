\set ON_ERROR_STOP on

BEGIN;

\ir schema_reversal_guard.sql

DO $$
BEGIN
    IF EXISTS (SELECT 1 FROM operation_logs WHERE user_id IS NULL) THEN
        RAISE EXCEPTION
            'V002 rollback blocked: operation_logs contains anonymous audit rows';
    END IF;
END
$$;

ALTER TABLE operation_logs
    DROP CONSTRAINT IF EXISTS fk_operation_logs_user_id;

ALTER TABLE operation_logs
    ALTER COLUMN user_id SET NOT NULL;

ALTER TABLE operation_logs
    ADD CONSTRAINT fk_operation_logs_user_id
        FOREIGN KEY (user_id) REFERENCES users (id) ON DELETE CASCADE;

COMMENT ON COLUMN operation_logs.user_id IS '用户ID';

COMMIT;
