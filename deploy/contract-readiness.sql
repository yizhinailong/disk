\set ON_ERROR_STOP on

\if :{?t_s3_only}
\else
    DO $$ BEGIN RAISE EXCEPTION 't_s3_only is required'; END $$;
\endif

\if :{?scan_id}
\else
    DO $$ BEGIN RAISE EXCEPTION 'scan_id is required'; END $$;
\endif

BEGIN TRANSACTION ISOLATION LEVEL REPEATABLE READ READ ONLY;

WITH blocker_counts AS (
    SELECT
        (
            SELECT COUNT(*)
            FROM upload_tasks
            WHERE created_at <= :'t_s3_only'::timestamp
              AND status IN (0, 4)
        ) AS pre_cutover_active,
        (
            SELECT COUNT(*)
            FROM upload_tasks
            WHERE staging_backend = 'local'
              AND status IN (0, 4)
        ) AS local_nonterminal,
        (
            SELECT COUNT(*)
            FROM storage_jobs
            WHERE job_type = 'staging_cleanup'
              AND payload->>'backend' = 'local'
              AND status <> 3
        ) AS local_cleanup_incomplete,
        (
            SELECT COUNT(*)
            FROM upload_tasks
            WHERE status = 4
               OR lease_owner IS NOT NULL
               OR lease_expires_at IS NOT NULL
        ) AS active_upload_leases,
        (
            SELECT COUNT(*)
            FROM upload_tasks
            WHERE staging_prefix IS NULL
        ) AS null_staging_prefix,
        (
            SELECT COUNT(*)
            FROM upload_task_chunks
            WHERE size_bytes IS NULL
               OR hash_md5 IS NULL
               OR object_key IS NULL
        ) AS nullable_chunk_compat_fields,
        (
            SELECT COUNT(*)
            FROM storage_jobs
            WHERE job_type IN ('expire_uploads', 'staging_cleanup', 'multipart_abort')
              AND status IN (0, 1, 2, 4)
        ) AS unfinished_upload_jobs,
        (
            SELECT COUNT(*)
            FROM storage_jobs
            WHERE job_type = 'storage_reconcile'
              AND aggregate_id = :'scan_id'
              AND status <> 3
        ) AS unfinished_reconciliation_jobs,
        (
            SELECT COUNT(*)
            FROM storage_jobs
            WHERE status <> 3
        ) AS unfinished_jobs_all_types,
        (
            SELECT COUNT(*)
            FROM storage_reconciliation_findings
            WHERE resolved_at IS NULL
        ) AS unresolved_findings,
        (
            SELECT COUNT(*)
            FROM users
            WHERE storage_used <>
                    (
                        COALESCE(
                            (SELECT SUM(size) FROM files WHERE user_id = users.id),
                            0
                        ) +
                        COALESCE(
                            (SELECT SUM(item_size) FROM trash WHERE user_id = users.id),
                            0
                        )
                    )
               OR storage_reserved <>
                    COALESCE(
                        (
                            SELECT SUM(reserved_bytes)
                            FROM upload_tasks
                            WHERE user_id = users.id
                              AND status IN (0, 4)
                        ),
                        0
                    )
        ) AS quota_mismatches,
        (
            SELECT COUNT(*)
            FROM file_contents AS content
            WHERE content.ref_count <>
                (
                    (SELECT COUNT(*) FROM files WHERE content_id = content.id) +
                    (
                        SELECT COUNT(*)
                        FROM trash
                        WHERE content_id = content.id
                          AND item_type = 'file'
                    )
                )
        ) AS content_ref_count_mismatches
), reconciliation_scopes AS (
    SELECT
        COALESCE(payload->>'scope', '<missing>') AS scope,
        COUNT(*) AS pages,
        BOOL_AND(status = 3) AS all_succeeded
    FROM storage_jobs
    WHERE job_type = 'storage_reconcile'
      AND aggregate_id = :'scan_id'
    GROUP BY COALESCE(payload->>'scope', '<missing>')
), reconciliation_summary AS (
    SELECT
        COUNT(*) AS scope_count,
        COUNT(*) FILTER (
            WHERE scope IN ('contents', 'users', 'staging', 'final')
        ) AS required_scope_count,
        COALESCE(BOOL_AND(all_succeeded), FALSE) AS all_pages_succeeded,
        COALESCE(
            JSONB_OBJECT_AGG(
                scope,
                JSONB_BUILD_OBJECT(
                    'pages', pages,
                    'all_succeeded', all_succeeded
                )
                ORDER BY scope
            ),
            '{}'::jsonb
        ) AS scopes
    FROM reconciliation_scopes
), readiness AS (
    SELECT
        TO_JSONB(blocker_counts) AS blockers,
        reconciliation_summary.scope_count = 4
            AND reconciliation_summary.required_scope_count = 4
            AND reconciliation_summary.all_pages_succeeded
            AND NOT EXISTS (
                SELECT 1
                FROM JSONB_EACH_TEXT(TO_JSONB(blocker_counts)) AS blocker
                WHERE blocker.value::bigint <> 0
            ) AS admitted,
        reconciliation_summary.scope_count,
        reconciliation_summary.required_scope_count,
        reconciliation_summary.all_pages_succeeded,
        reconciliation_summary.scopes
    FROM blocker_counts
    CROSS JOIN reconciliation_summary
)
SELECT JSONB_BUILD_OBJECT(
    'schema_version', 1,
    'observed_at', CURRENT_TIMESTAMP,
    'inputs', JSONB_BUILD_OBJECT(
        't_s3_only', :'t_s3_only'::timestamp,
        'scan_id', :'scan_id'
    ),
    'blockers', readiness.blockers,
    'reconciliation', JSONB_BUILD_OBJECT(
        'scope_count', readiness.scope_count,
        'required_scope_count', readiness.required_scope_count,
        'all_pages_succeeded', readiness.all_pages_succeeded,
        'scopes', readiness.scopes
    ),
    'contract_design_review_admitted', readiness.admitted,
    'compatibility_removal_allowed', FALSE
)
FROM readiness;

COMMIT;
