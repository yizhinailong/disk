BEGIN;

ALTER TABLE operation_logs
    DROP CONSTRAINT IF EXISTS fk_operation_logs_user_id;

ALTER TABLE operation_logs
    DROP CONSTRAINT IF EXISTS operation_logs_user_id_fkey;

ALTER TABLE operation_logs
    ALTER COLUMN user_id DROP NOT NULL;

ALTER TABLE operation_logs
    ADD CONSTRAINT fk_operation_logs_user_id
        FOREIGN KEY (user_id) REFERENCES users (id) ON DELETE SET NULL;

COMMENT ON COLUMN operation_logs.user_id IS
    '操作者用户ID，公开访客或已删除用户为NULL';

COMMIT;
