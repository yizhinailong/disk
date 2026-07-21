\set ON_ERROR_STOP on

\if :{?stage_started_at}
\else
    \echo 'stage_started_at is required'
    \quit 3
\endif

\if :{?stage_ended_at}
\else
    \echo 'stage_ended_at is required'
    \quit 3
\endif

BEGIN TRANSACTION ISOLATION LEVEL REPEATABLE READ READ ONLY;

SELECT 'preexisting_descriptors' AS query_id,
       id,
       staging_backend,
       staging_prefix,
       status,
       state_version,
       created_at
FROM upload_tasks
WHERE created_at < :'stage_started_at'::timestamp
ORDER BY id;

SELECT 'stage_uploads' AS query_id,
       id,
       staging_backend,
       staging_prefix,
       status,
       state_version,
       finalize_attempts,
       completed_file_id,
       lease_owner,
       lease_expires_at,
       reserved_bytes,
       created_at,
       finalized_at
FROM upload_tasks
WHERE created_at >= :'stage_started_at'::timestamp
  AND created_at < :'stage_ended_at'::timestamp
ORDER BY id;

SELECT 'stage_backend_counts' AS query_id,
       count(*) AS total_tasks,
       count(*) FILTER (WHERE staging_backend = 'local') AS local_tasks,
       count(*) FILTER (WHERE staging_backend = 's3') AS s3_tasks,
       round(
           100.0 * count(*) FILTER (WHERE staging_backend = 's3') /
               NULLIF(count(*), 0),
           3
       ) AS actual_s3_percent
FROM upload_tasks
WHERE created_at >= :'stage_started_at'::timestamp
  AND created_at < :'stage_ended_at'::timestamp;

SELECT 'stage_cleanup_jobs' AS query_id,
       task.id AS upload_id,
       task.staging_backend,
       task.status AS upload_status,
       job.id AS job_id,
       job.status AS job_status,
       job.attempts,
       job.locked_by,
       job.locked_until,
       job.completed_at
FROM upload_tasks AS task
LEFT JOIN storage_jobs AS job
       ON job.dedupe_key = 'staging-cleanup:' || task.id
WHERE task.created_at >= :'stage_started_at'::timestamp
  AND task.created_at < :'stage_ended_at'::timestamp
ORDER BY task.id;

SELECT 'cluster_blockers' AS query_id,
       id,
       job_type,
       status,
       attempts,
       locked_by,
       locked_until,
       available_at,
       completed_at
FROM storage_jobs
WHERE status = 4
   OR (status = 1 AND locked_until <= CURRENT_TIMESTAMP)
ORDER BY id;

SELECT 'stage_quota_and_content_references' AS query_id,
       task.id AS upload_id,
       task.status AS upload_status,
       account.id AS user_id,
       account.storage_quota,
       account.storage_used,
       account.storage_reserved,
       file.id AS file_id,
       content.id AS content_id,
       content.ref_count,
       count(all_files.id) AS current_file_references
FROM upload_tasks AS task
JOIN users AS account ON account.id = task.user_id
LEFT JOIN files AS file ON file.id = task.completed_file_id
LEFT JOIN file_contents AS content ON content.id = file.content_id
LEFT JOIN files AS all_files ON all_files.content_id = content.id
WHERE task.created_at >= :'stage_started_at'::timestamp
  AND task.created_at < :'stage_ended_at'::timestamp
GROUP BY task.id,
         task.status,
         account.id,
         account.storage_quota,
         account.storage_used,
         account.storage_reserved,
         file.id,
         content.id,
         content.ref_count
ORDER BY task.id;

SELECT 'unresolved_findings' AS query_id,
       id,
       finding_type,
       resource_id,
       severity,
       resolution_strategy,
       occurrences,
       first_seen_at,
       last_seen_at
FROM storage_reconciliation_findings
WHERE resolved_at IS NULL
ORDER BY severity DESC, last_seen_at, id;

COMMIT;
