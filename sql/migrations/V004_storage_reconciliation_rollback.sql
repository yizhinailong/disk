\set ON_ERROR_STOP on

BEGIN;

DO $rollback$
DECLARE
    has_findings BOOLEAN := FALSE;
BEGIN
    IF to_regclass('public.storage_reconciliation_findings') IS NOT NULL THEN
        EXECUTE 'SELECT EXISTS (SELECT 1 FROM storage_reconciliation_findings)'
            INTO has_findings;
        IF has_findings THEN
            RAISE EXCEPTION
                'V004 rollback blocked: storage_reconciliation_findings contains records';
        END IF;
    END IF;
END
$rollback$;

DROP TABLE IF EXISTS storage_reconciliation_findings;

DO $rollback$
BEGIN
    IF to_regclass('public.schema_migrations') IS NOT NULL THEN
        DELETE FROM schema_migrations
        WHERE version = 'V004_storage_reconciliation';
    END IF;
END
$rollback$;

COMMIT;
