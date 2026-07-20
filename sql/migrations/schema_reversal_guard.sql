\if :{?disk_schema_reversal_context}
\else
\set disk_schema_reversal_context ''
\endif

\if :{?disk_schema_reversal_approved}
\else
\set disk_schema_reversal_approved ''
\endif

\if :{?disk_schema_change_ticket}
\else
\set disk_schema_change_ticket ''
\endif

\if :{?disk_schema_readiness_sha256}
\else
\set disk_schema_readiness_sha256 ''
\endif

SELECT
    set_config(
        'disk.schema_reversal_context',
        :'disk_schema_reversal_context',
        true
    ) AS guard_context,
    set_config(
        'disk.schema_reversal_approved',
        :'disk_schema_reversal_approved',
        true
    ) AS guard_approval,
    set_config(
        'disk.schema_change_ticket',
        :'disk_schema_change_ticket',
        true
    ) AS guard_ticket,
    set_config(
        'disk.schema_readiness_sha256',
        :'disk_schema_readiness_sha256',
        true
    ) AS guard_evidence
\gset

DO $schema_reversal_guard$
DECLARE
    reversal_context TEXT := current_setting('disk.schema_reversal_context', true);
    reversal_approved TEXT := current_setting('disk.schema_reversal_approved', true);
    change_ticket TEXT := current_setting('disk.schema_change_ticket', true);
    readiness_sha256 TEXT := current_setting('disk.schema_readiness_sha256', true);
BEGIN
    IF reversal_context IS DISTINCT FROM 'pre_activation_reversal' THEN
        RAISE EXCEPTION
            'schema reversal blocked: context must be pre_activation_reversal; emergency application rollback preserves expand schema';
    END IF;

    IF reversal_approved IS DISTINCT FROM 'true' THEN
        RAISE EXCEPTION
            'schema reversal blocked: explicit independent approval is required';
    END IF;

    IF change_ticket IS NULL
       OR change_ticket !~ '^[A-Za-z0-9][A-Za-z0-9._:/-]{5,127}$' THEN
        RAISE EXCEPTION
            'schema reversal blocked: a valid schema change ticket is required';
    END IF;

    IF readiness_sha256 IS NULL
       OR readiness_sha256 !~ '^[0-9a-f]{64}$' THEN
        RAISE EXCEPTION
            'schema reversal blocked: a lowercase SHA-256 readiness evidence digest is required';
    END IF;
END
$schema_reversal_guard$;
